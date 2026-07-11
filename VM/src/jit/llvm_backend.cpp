#include "jit/llvm_backend.h"

#include <cstdio>
#include <cstring>
#include <cstdint>
#include <limits>
#include <memory>
#include <mutex>
#include <sstream>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

#include "ffi/dl_call.h"
#include "interpreter/dispatch.h"
#include "intrinsic_ids.h"
#include "jit/call_context.h"
#include "native/registry.h"
#include "native/time.h"
#include "opcode.h"
#include "runtime/abi.h"
#include "runtime/import_dispatch.h"
#include "runtime/print_any.h"
#include "runtime/values.h"
#include "sbc_loader.h"

#if defined(SIMPLEVM_HAS_LLVM)
#include <llvm/ADT/StringRef.h>
#include <llvm/ExecutionEngine/Orc/LLJIT.h>
#include <llvm/ExecutionEngine/Orc/Mangling.h>
#include <llvm/ExecutionEngine/Orc/ThreadSafeModule.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/DerivedTypes.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/Intrinsics.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Verifier.h>
#include <llvm/Support/Error.h>
#include <llvm/Support/TargetSelect.h>
#include <llvm/Support/raw_ostream.h>
#endif

namespace Simple::VM::Jit {
namespace {

using Simple::Byte::OpCode;
using Slot = Simple::VM::Interpreter::Slot;
using Simple::VM::Interpreter::ReadI32;
using Simple::VM::Interpreter::ReadI64;
using Simple::VM::Interpreter::ReadU8;
using Simple::VM::Interpreter::ReadU16;
using Simple::VM::Interpreter::ReadU32;
using Simple::VM::Interpreter::ReadU64;
using Simple::VM::Runtime::PackI32;

#if defined(SIMPLEVM_HAS_LLVM)
thread_local bool g_llvm_trap = false;
thread_local Simple::VM::Heap* g_llvm_heap = nullptr;
thread_local std::vector<Slot>* g_llvm_globals = nullptr;
thread_local const Simple::Byte::SbcModule* g_llvm_module = nullptr;
thread_local const Simple::VM::ExecOptions* g_llvm_exec_options = nullptr;
thread_local std::vector<Simple::VM::Native::NativeHandleId> g_llvm_file_handles;
thread_local Simple::VM::Native::NativeResourceRegistry g_llvm_resource_registry;
thread_local std::string g_llvm_dl_last_error;

using EntryFn = uint64_t (*)(uint64_t*, uint32_t);

struct CachedLlvmEntry {
  std::unique_ptr<llvm::orc::LLJIT> jit;
  EntryFn entry = nullptr;
  bool has_ret = true;
};

// Intentionally leaked: ORC/LLVM teardown order at process exit is subtle, and
// cached native code is process-lifetime state for `svm` anyway.
std::mutex& LlvmCacheMutex() {
  static auto* mutex = new std::mutex();
  return *mutex;
}

std::unordered_map<std::string, std::shared_ptr<CachedLlvmEntry>>& LlvmCache() {
  static auto* cache = new std::unordered_map<std::string, std::shared_ptr<CachedLlvmEntry>>();
  return *cache;
}

std::unordered_map<std::string, std::string>& LlvmRejectCache() {
  static auto* cache = new std::unordered_map<std::string, std::string>();
  return *cache;
}

bool LlvmTypeIdIsVoidLike(const Simple::Byte::SbcModule& module, uint32_t type_id) {
  if (type_id == 0xFFFFFFFFu) return true;
  if (type_id >= module.types.size()) return false;
  const auto kind = static_cast<Simple::Byte::TypeKind>(module.types[type_id].kind);
  return kind == Simple::Byte::TypeKind::Void || kind == Simple::Byte::TypeKind::Unspecified;
}

extern "C" void SimpleVmLlvmTrap() {
  g_llvm_trap = true;
}

extern "C" void SimpleVmLlvmYield() {
  std::this_thread::yield();
}

extern "C" void SimpleVmLlvmPrintAny(uint64_t value, uint32_t tag) {
  if (!g_llvm_heap) { g_llvm_trap = true; return; }
  std::string print_error;
  if (!Simple::VM::Runtime::PrintAny(*g_llvm_heap, tag, value, &print_error)) {
    g_llvm_trap = true;
    return;
  }
  std::fflush(stdout);
}

extern "C" uint64_t SimpleVmLlvmMonoNs() {
  return Simple::VM::Runtime::PackI64(Simple::VM::Native::Time::MonotonicNs());
}

extern "C" uint64_t SimpleVmLlvmWallNs() {
  return Simple::VM::Runtime::PackI64(Simple::VM::Native::Time::WallNs());
}

extern "C" uint64_t SimpleVmLlvmConstString(uint32_t const_id) {
  if (!g_llvm_heap || !g_llvm_module || const_id + 8 > g_llvm_module->const_pool.size()) {
    g_llvm_trap = true;
    return 0;
  }
  uint32_t kind = Simple::VM::ReadU32Payload(g_llvm_module->const_pool, const_id);
  if (kind != 0) { g_llvm_trap = true; return 0; }
  uint32_t str_offset = Simple::VM::ReadU32Payload(g_llvm_module->const_pool, const_id + 4);
  if (str_offset >= g_llvm_module->const_pool.size()) { g_llvm_trap = true; return 0; }
  const char* base = reinterpret_cast<const char*>(g_llvm_module->const_pool.data() + str_offset);
  std::u16string text;
  for (size_t i = 0; str_offset + i < g_llvm_module->const_pool.size(); ++i) {
    char c = base[i];
    if (c == '\0') break;
    text.push_back(static_cast<char16_t>(static_cast<unsigned char>(c)));
  }
  uint32_t handle = Simple::VM::CreateString(*g_llvm_heap, text);
  if (handle == 0xFFFFFFFFu) { g_llvm_trap = true; return 0; }
  return Simple::VM::Runtime::PackRef(handle);
}

extern "C" uint64_t SimpleVmLlvmStringLen(uint64_t ref_slot) {
  if (!g_llvm_heap || Simple::VM::Runtime::IsNullRef(ref_slot)) { g_llvm_trap = true; return 0; }
  Simple::VM::HeapObject* obj = g_llvm_heap->Get(Simple::VM::Runtime::UnpackRef(ref_slot));
  if (!obj || obj->header.kind != Simple::VM::ObjectKind::String) { g_llvm_trap = true; return 0; }
  return Simple::VM::Runtime::PackI32(static_cast<int32_t>(Simple::VM::ReadString(obj).size()));
}

extern "C" uint64_t SimpleVmLlvmStackTrace() {
  if (!g_llvm_heap) { g_llvm_trap = true; return 0; }
  uint32_t handle = Simple::VM::CreateString(*g_llvm_heap, Simple::VM::AsciiToU16("<llvm-jit>"));
  if (handle == 0xFFFFFFFFu) { g_llvm_trap = true; return 0; }
  return Simple::VM::Runtime::PackRef(handle);
}

extern "C" uint64_t SimpleVmLlvmStringConcat(uint64_t lhs_slot, uint64_t rhs_slot) {
  if (!g_llvm_heap || Simple::VM::Runtime::IsNullRef(lhs_slot) || Simple::VM::Runtime::IsNullRef(rhs_slot)) { g_llvm_trap = true; return 0; }
  Simple::VM::HeapObject* lhs = g_llvm_heap->Get(Simple::VM::Runtime::UnpackRef(lhs_slot));
  Simple::VM::HeapObject* rhs = g_llvm_heap->Get(Simple::VM::Runtime::UnpackRef(rhs_slot));
  if (!lhs || !rhs || lhs->header.kind != Simple::VM::ObjectKind::String || rhs->header.kind != Simple::VM::ObjectKind::String) { g_llvm_trap = true; return 0; }
  std::u16string text = Simple::VM::ReadString(lhs);
  std::u16string r = Simple::VM::ReadString(rhs);
  text.insert(text.end(), r.begin(), r.end());
  uint32_t handle = Simple::VM::CreateString(*g_llvm_heap, text);
  if (handle == 0xFFFFFFFFu) { g_llvm_trap = true; return 0; }
  return Simple::VM::Runtime::PackRef(handle);
}

extern "C" uint64_t SimpleVmLlvmStringCompare(uint64_t lhs_slot, uint64_t rhs_slot, uint32_t op) {
  if (!g_llvm_heap || Simple::VM::Runtime::IsNullRef(lhs_slot) || Simple::VM::Runtime::IsNullRef(rhs_slot)) { g_llvm_trap = true; return 0; }
  Simple::VM::HeapObject* lhs = g_llvm_heap->Get(Simple::VM::Runtime::UnpackRef(lhs_slot));
  Simple::VM::HeapObject* rhs = g_llvm_heap->Get(Simple::VM::Runtime::UnpackRef(rhs_slot));
  if (!lhs || !rhs || lhs->header.kind != Simple::VM::ObjectKind::String || rhs->header.kind != Simple::VM::ObjectKind::String) { g_llvm_trap = true; return 0; }
  std::u16string a = Simple::VM::ReadString(lhs);
  std::u16string b = Simple::VM::ReadString(rhs);
  if (op == 0) return Simple::VM::Runtime::PackI32(a == b ? 1 : 0);
  if (op == 1) return Simple::VM::Runtime::PackI32(a != b ? 1 : 0);
  if (op == 2) return Simple::VM::Runtime::PackI32(a.compare(b));
  auto pos = a.find(b);
  return Simple::VM::Runtime::PackI32(pos == std::u16string::npos ? -1 : static_cast<int32_t>(pos));
}

extern "C" uint64_t SimpleVmLlvmStringGetChar(uint64_t ref_slot, uint64_t index_slot) {
  if (!g_llvm_heap || Simple::VM::Runtime::IsNullRef(ref_slot)) { g_llvm_trap = true; return 0; }
  Simple::VM::HeapObject* obj = g_llvm_heap->Get(Simple::VM::Runtime::UnpackRef(ref_slot));
  if (!obj || obj->header.kind != Simple::VM::ObjectKind::String) { g_llvm_trap = true; return 0; }
  std::u16string text = Simple::VM::ReadString(obj);
  int32_t index = Simple::VM::Runtime::UnpackI32(index_slot);
  if (index < 0 || static_cast<size_t>(index) >= text.size()) { g_llvm_trap = true; return 0; }
  return Simple::VM::Runtime::PackI32(static_cast<int32_t>(text[static_cast<size_t>(index)]));
}

extern "C" uint64_t SimpleVmLlvmStringSlice(uint64_t ref_slot, uint64_t start_slot, uint64_t end_slot) {
  if (!g_llvm_heap || Simple::VM::Runtime::IsNullRef(ref_slot)) { g_llvm_trap = true; return 0; }
  Simple::VM::HeapObject* obj = g_llvm_heap->Get(Simple::VM::Runtime::UnpackRef(ref_slot));
  if (!obj || obj->header.kind != Simple::VM::ObjectKind::String) { g_llvm_trap = true; return 0; }
  std::u16string text = Simple::VM::ReadString(obj);
  int32_t start = Simple::VM::Runtime::UnpackI32(start_slot);
  int32_t end = Simple::VM::Runtime::UnpackI32(end_slot);
  if (start < 0 || end < start || static_cast<size_t>(end) > text.size()) { g_llvm_trap = true; return 0; }
  uint32_t handle = Simple::VM::CreateString(*g_llvm_heap, text.substr(static_cast<size_t>(start), static_cast<size_t>(end - start)));
  if (handle == 0xFFFFFFFFu) { g_llvm_trap = true; return 0; }
  return Simple::VM::Runtime::PackRef(handle);
}

extern "C" uint64_t SimpleVmLlvmLoadGlobal(uint32_t index) {
  if (!g_llvm_globals || index >= g_llvm_globals->size()) {
    g_llvm_trap = true;
    return 0;
  }
  return (*g_llvm_globals)[index];
}

extern "C" void SimpleVmLlvmStoreGlobal(uint32_t index, uint64_t value) {
  if (!g_llvm_globals || index >= g_llvm_globals->size()) {
    g_llvm_trap = true;
    return;
  }
  (*g_llvm_globals)[index] = value;
}

extern "C" uint64_t SimpleVmLlvmNewObject(uint32_t type_id, uint32_t size) {
  if (!g_llvm_heap) { g_llvm_trap = true; return 0; }
  uint32_t handle = g_llvm_heap->Allocate(Simple::VM::ObjectKind::Artifact, type_id, size);
  if (!g_llvm_heap->Get(handle)) { g_llvm_trap = true; return 0; }
  return Simple::VM::Runtime::PackRef(handle);
}

extern "C" uint64_t SimpleVmLlvmLoadField32(uint64_t ref_slot, uint32_t offset) {
  if (!g_llvm_heap || Simple::VM::Runtime::IsNullRef(ref_slot)) { g_llvm_trap = true; return 0; }
  Simple::VM::HeapObject* obj = g_llvm_heap->Get(Simple::VM::Runtime::UnpackRef(ref_slot));
  if (!obj || obj->header.kind != Simple::VM::ObjectKind::Artifact || offset + 4u > obj->payload.size()) {
    g_llvm_trap = true;
    return 0;
  }
  return Simple::VM::ReadU32Payload(obj->payload, offset);
}

extern "C" void SimpleVmLlvmStoreField32(uint64_t ref_slot, uint32_t offset, uint64_t value_slot) {
  if (!g_llvm_heap || Simple::VM::Runtime::IsNullRef(ref_slot)) { g_llvm_trap = true; return; }
  Simple::VM::HeapObject* obj = g_llvm_heap->Get(Simple::VM::Runtime::UnpackRef(ref_slot));
  if (!obj || obj->header.kind != Simple::VM::ObjectKind::Artifact || offset + 4u > obj->payload.size()) {
    g_llvm_trap = true;
    return;
  }
  Simple::VM::WriteU32Payload(obj->payload, offset, static_cast<uint32_t>(value_slot));
}

extern "C" uint64_t SimpleVmLlvmTypeOf(uint64_t ref_slot) {
  if (!g_llvm_heap || Simple::VM::Runtime::IsNullRef(ref_slot)) { g_llvm_trap = true; return 0; }
  Simple::VM::HeapObject* obj = g_llvm_heap->Get(Simple::VM::Runtime::UnpackRef(ref_slot));
  if (!obj) { g_llvm_trap = true; return 0; }
  return Simple::VM::Runtime::PackI32(static_cast<int32_t>(obj->header.type_id));
}

extern "C" uint64_t SimpleVmLlvmCheckedRef(uint64_t ref_slot) {
  if (!g_llvm_heap || Simple::VM::Runtime::IsNullRef(ref_slot) ||
      !g_llvm_heap->Get(Simple::VM::Runtime::UnpackRef(ref_slot))) {
    g_llvm_trap = true;
    return 0;
  }
  return ref_slot;
}

extern "C" uint64_t SimpleVmLlvmCloneObject(uint64_t ref_slot) {
  if (!g_llvm_heap || Simple::VM::Runtime::IsNullRef(ref_slot)) { g_llvm_trap = true; return 0; }
  Simple::VM::HeapObject* obj = g_llvm_heap->Get(Simple::VM::Runtime::UnpackRef(ref_slot));
  if (!obj) { g_llvm_trap = true; return 0; }
  const Simple::VM::ObjectKind kind = obj->header.kind;
  const uint32_t type_id = obj->header.type_id;
  const uint32_t size = obj->header.size;
  const std::vector<uint8_t> payload = obj->payload;
  uint32_t handle = g_llvm_heap->Allocate(kind, type_id, size);
  Simple::VM::HeapObject* clone = g_llvm_heap->Get(handle);
  if (!clone) { g_llvm_trap = true; return 0; }
  clone->payload = payload;
  return Simple::VM::Runtime::PackRef(handle);
}

extern "C" uint64_t SimpleVmLlvmObjectEq(uint64_t lhs_slot, uint64_t rhs_slot) {
  if (!g_llvm_heap || Simple::VM::Runtime::IsNullRef(lhs_slot) || Simple::VM::Runtime::IsNullRef(rhs_slot)) {
    g_llvm_trap = true;
    return 0;
  }
  Simple::VM::HeapObject* lhs = g_llvm_heap->Get(Simple::VM::Runtime::UnpackRef(lhs_slot));
  Simple::VM::HeapObject* rhs = g_llvm_heap->Get(Simple::VM::Runtime::UnpackRef(rhs_slot));
  if (!lhs || !rhs) { g_llvm_trap = true; return 0; }
  bool equal = false;
  if (lhs->header.kind == Simple::VM::ObjectKind::String && rhs->header.kind == Simple::VM::ObjectKind::String) {
    equal = Simple::VM::ReadString(lhs) == Simple::VM::ReadString(rhs);
  } else {
    equal = lhs->header.kind == rhs->header.kind && lhs->header.type_id == rhs->header.type_id &&
            lhs->payload == rhs->payload;
  }
  return Simple::VM::Runtime::PackI32(equal ? 1 : 0);
}

extern "C" uint64_t SimpleVmLlvmInstanceOf(uint64_t ref_slot, uint64_t type_slot) {
  if (!g_llvm_heap) { g_llvm_trap = true; return 0; }
  if (Simple::VM::Runtime::IsNullRef(ref_slot)) return Simple::VM::Runtime::PackI32(0);
  Simple::VM::HeapObject* obj = g_llvm_heap->Get(Simple::VM::Runtime::UnpackRef(ref_slot));
  if (!obj) { g_llvm_trap = true; return 0; }
  return Simple::VM::Runtime::PackI32(obj->header.type_id == static_cast<uint32_t>(Simple::VM::Runtime::UnpackI32(type_slot)) ? 1 : 0);
}

extern "C" uint64_t SimpleVmLlvmCheckedCastRef(uint64_t ref_slot, uint64_t type_slot) {
  if (!g_llvm_heap || Simple::VM::Runtime::IsNullRef(ref_slot)) { g_llvm_trap = true; return 0; }
  Simple::VM::HeapObject* obj = g_llvm_heap->Get(Simple::VM::Runtime::UnpackRef(ref_slot));
  if (!obj || obj->header.type_id != static_cast<uint32_t>(Simple::VM::Runtime::UnpackI32(type_slot))) {
    g_llvm_trap = true;
    return 0;
  }
  return ref_slot;
}

extern "C" uint64_t SimpleVmLlvmNewArray(uint32_t type_id, uint32_t length, uint32_t element_size) {
  if (!g_llvm_heap || element_size == 0) {
    g_llvm_trap = true;
    return 0;
  }
  uint64_t bytes = 4ull + static_cast<uint64_t>(length) * static_cast<uint64_t>(element_size);
  if (bytes > static_cast<uint64_t>(std::numeric_limits<uint32_t>::max())) {
    g_llvm_trap = true;
    return 0;
  }
  uint32_t handle = g_llvm_heap->Allocate(Simple::VM::ObjectKind::Array, type_id, static_cast<uint32_t>(bytes));
  Simple::VM::HeapObject* obj = g_llvm_heap->Get(handle);
  if (!obj) {
    g_llvm_trap = true;
    return 0;
  }
  Simple::VM::WriteU32Payload(obj->payload, 0, length);
  return Simple::VM::Runtime::PackRef(handle);
}

extern "C" uint64_t SimpleVmLlvmArrayLen(uint64_t ref_slot) {
  if (!g_llvm_heap || Simple::VM::Runtime::IsNullRef(ref_slot)) {
    g_llvm_trap = true;
    return 0;
  }
  Simple::VM::HeapObject* obj = g_llvm_heap->Get(Simple::VM::Runtime::UnpackRef(ref_slot));
  if (!obj || obj->header.kind != Simple::VM::ObjectKind::Array) {
    g_llvm_trap = true;
    return 0;
  }
  return Simple::VM::Runtime::PackI32(static_cast<int32_t>(Simple::VM::ReadU32Payload(obj->payload, 0)));
}

static Simple::VM::HeapObject* SimpleVmLlvmCheckedArray(uint64_t ref_slot, uint64_t index_slot,
                                                        uint32_t& length, int32_t& index) {
  if (!g_llvm_heap || Simple::VM::Runtime::IsNullRef(ref_slot)) {
    g_llvm_trap = true;
    return nullptr;
  }
  Simple::VM::HeapObject* obj = g_llvm_heap->Get(Simple::VM::Runtime::UnpackRef(ref_slot));
  if (!obj || obj->header.kind != Simple::VM::ObjectKind::Array) {
    g_llvm_trap = true;
    return nullptr;
  }
  length = Simple::VM::ReadU32Payload(obj->payload, 0);
  index = Simple::VM::Runtime::UnpackI32(index_slot);
  if (index < 0 || static_cast<uint32_t>(index) >= length) {
    g_llvm_trap = true;
    return nullptr;
  }
  return obj;
}

extern "C" uint64_t SimpleVmLlvmArrayGetI32(uint64_t ref_slot, uint64_t index_slot) {
  uint32_t length = 0;
  int32_t index = 0;
  Simple::VM::HeapObject* obj = SimpleVmLlvmCheckedArray(ref_slot, index_slot, length, index);
  if (!obj) return 0;
  int32_t value = static_cast<int32_t>(Simple::VM::ReadU32Payload(obj->payload, 4 + static_cast<size_t>(index) * 4u));
  return Simple::VM::Runtime::PackI32(value);
}

extern "C" uint64_t SimpleVmLlvmArrayGetI64(uint64_t ref_slot, uint64_t index_slot) {
  uint32_t length = 0;
  int32_t index = 0;
  Simple::VM::HeapObject* obj = SimpleVmLlvmCheckedArray(ref_slot, index_slot, length, index);
  if (!obj) return 0;
  int64_t value = static_cast<int64_t>(Simple::VM::ReadU64Payload(obj->payload, 4 + static_cast<size_t>(index) * 8u));
  return Simple::VM::Runtime::PackI64(value);
}

extern "C" uint64_t SimpleVmLlvmArrayGetF32(uint64_t ref_slot, uint64_t index_slot) {
  if (!g_llvm_heap || Simple::VM::Runtime::IsNullRef(ref_slot)) {
    g_llvm_trap = true;
    return 0;
  }
  Simple::VM::HeapObject* obj = g_llvm_heap->Get(Simple::VM::Runtime::UnpackRef(ref_slot));
  if (!obj || obj->header.kind != Simple::VM::ObjectKind::Array) {
    g_llvm_trap = true;
    return 0;
  }
  uint32_t length = Simple::VM::ReadU32Payload(obj->payload, 0);
  int32_t index = Simple::VM::Runtime::UnpackI32(index_slot);
  if (index < 0 || static_cast<uint32_t>(index) >= length) {
    g_llvm_trap = true;
    return 0;
  }
  uint32_t bits = Simple::VM::ReadU32Payload(obj->payload, 4 + static_cast<size_t>(index) * 4u);
  return Simple::VM::Runtime::PackF32Bits(bits);
}

extern "C" uint64_t SimpleVmLlvmArrayGetF64(uint64_t ref_slot, uint64_t index_slot) {
  uint32_t length = 0;
  int32_t index = 0;
  Simple::VM::HeapObject* obj = SimpleVmLlvmCheckedArray(ref_slot, index_slot, length, index);
  if (!obj) return 0;
  uint64_t bits = Simple::VM::ReadU64Payload(obj->payload, 4 + static_cast<size_t>(index) * 8u);
  return Simple::VM::Runtime::PackF64Bits(bits);
}

extern "C" uint64_t SimpleVmLlvmArrayGetRef(uint64_t ref_slot, uint64_t index_slot) {
  uint32_t length = 0;
  int32_t index = 0;
  Simple::VM::HeapObject* obj = SimpleVmLlvmCheckedArray(ref_slot, index_slot, length, index);
  if (!obj) return 0;
  uint32_t handle = Simple::VM::ReadU32Payload(obj->payload, 4 + static_cast<size_t>(index) * 4u);
  return Simple::VM::Runtime::PackRef(handle);
}

extern "C" void SimpleVmLlvmArraySetI32(uint64_t ref_slot, uint64_t index_slot, uint64_t value_slot) {
  uint32_t length = 0;
  int32_t index = 0;
  Simple::VM::HeapObject* obj = SimpleVmLlvmCheckedArray(ref_slot, index_slot, length, index);
  if (!obj) return;
  Simple::VM::WriteU32Payload(obj->payload, 4 + static_cast<size_t>(index) * 4u,
                              static_cast<uint32_t>(Simple::VM::Runtime::UnpackI32(value_slot)));
}

extern "C" void SimpleVmLlvmArraySetI64(uint64_t ref_slot, uint64_t index_slot, uint64_t value_slot) {
  uint32_t length = 0;
  int32_t index = 0;
  Simple::VM::HeapObject* obj = SimpleVmLlvmCheckedArray(ref_slot, index_slot, length, index);
  if (!obj) return;
  Simple::VM::WriteU64Payload(obj->payload, 4 + static_cast<size_t>(index) * 8u,
                              static_cast<uint64_t>(Simple::VM::Runtime::UnpackI64(value_slot)));
}

extern "C" void SimpleVmLlvmArraySetF32(uint64_t ref_slot, uint64_t index_slot, uint64_t value_slot) {
  if (!g_llvm_heap || Simple::VM::Runtime::IsNullRef(ref_slot)) {
    g_llvm_trap = true;
    return;
  }
  Simple::VM::HeapObject* obj = g_llvm_heap->Get(Simple::VM::Runtime::UnpackRef(ref_slot));
  if (!obj || obj->header.kind != Simple::VM::ObjectKind::Array) {
    g_llvm_trap = true;
    return;
  }
  uint32_t length = Simple::VM::ReadU32Payload(obj->payload, 0);
  int32_t index = Simple::VM::Runtime::UnpackI32(index_slot);
  if (index < 0 || static_cast<uint32_t>(index) >= length) {
    g_llvm_trap = true;
    return;
  }
  Simple::VM::WriteU32Payload(obj->payload, 4 + static_cast<size_t>(index) * 4u,
                              Simple::VM::Runtime::UnpackU32Bits(value_slot));
}

extern "C" void SimpleVmLlvmArraySetF64(uint64_t ref_slot, uint64_t index_slot, uint64_t value_slot) {
  uint32_t length = 0;
  int32_t index = 0;
  Simple::VM::HeapObject* obj = SimpleVmLlvmCheckedArray(ref_slot, index_slot, length, index);
  if (!obj) return;
  Simple::VM::WriteU64Payload(obj->payload, 4 + static_cast<size_t>(index) * 8u,
                              Simple::VM::Runtime::UnpackU64Bits(value_slot));
}

extern "C" void SimpleVmLlvmArraySetRef(uint64_t ref_slot, uint64_t index_slot, uint64_t value_slot) {
  uint32_t length = 0;
  int32_t index = 0;
  Simple::VM::HeapObject* obj = SimpleVmLlvmCheckedArray(ref_slot, index_slot, length, index);
  if (!obj) return;
  Simple::VM::WriteU32Payload(obj->payload, 4 + static_cast<size_t>(index) * 4u,
                              Simple::VM::Runtime::UnpackRef(value_slot));
}

extern "C" void SimpleVmLlvmArrayCopy(uint64_t src_slot, uint64_t src_index_slot,
                                      uint64_t dst_slot, uint64_t dst_index_slot,
                                      uint64_t count_slot) {
  if (!g_llvm_heap || Simple::VM::Runtime::IsNullRef(src_slot) || Simple::VM::Runtime::IsNullRef(dst_slot)) {
    g_llvm_trap = true;
    return;
  }
  Simple::VM::HeapObject* src = g_llvm_heap->Get(Simple::VM::Runtime::UnpackRef(src_slot));
  Simple::VM::HeapObject* dst = g_llvm_heap->Get(Simple::VM::Runtime::UnpackRef(dst_slot));
  if (!src || !dst || src->header.kind != Simple::VM::ObjectKind::Array ||
      dst->header.kind != Simple::VM::ObjectKind::Array || src->header.type_id != dst->header.type_id) {
    g_llvm_trap = true;
    return;
  }
  uint32_t src_len = Simple::VM::ReadU32Payload(src->payload, 0);
  uint32_t dst_len = Simple::VM::ReadU32Payload(dst->payload, 0);
  int32_t src_index = Simple::VM::Runtime::UnpackI32(src_index_slot);
  int32_t dst_index = Simple::VM::Runtime::UnpackI32(dst_index_slot);
  int32_t count = Simple::VM::Runtime::UnpackI32(count_slot);
  if (src_index < 0 || dst_index < 0 || count < 0 ||
      static_cast<uint32_t>(src_index) + static_cast<uint32_t>(count) > src_len ||
      static_cast<uint32_t>(dst_index) + static_cast<uint32_t>(count) > dst_len) {
    g_llvm_trap = true;
    return;
  }
  uint32_t elem_size = (src->payload.size() == 4u + static_cast<size_t>(src_len) * 8u) ? 8u : 4u;
  size_t bytes = static_cast<size_t>(count) * elem_size;
  size_t src_offset = 4u + static_cast<size_t>(src_index) * elem_size;
  size_t dst_offset = 4u + static_cast<size_t>(dst_index) * elem_size;
  if (src_offset + bytes > src->payload.size() || dst_offset + bytes > dst->payload.size()) {
    g_llvm_trap = true;
    return;
  }
  std::memmove(dst->payload.data() + dst_offset, src->payload.data() + src_offset, bytes);
}

extern "C" void SimpleVmLlvmArrayFill(uint64_t ref_slot, uint64_t count_slot, uint64_t fill_slot) {
  if (!g_llvm_heap || Simple::VM::Runtime::IsNullRef(ref_slot)) {
    g_llvm_trap = true;
    return;
  }
  Simple::VM::HeapObject* obj = g_llvm_heap->Get(Simple::VM::Runtime::UnpackRef(ref_slot));
  if (!obj || obj->header.kind != Simple::VM::ObjectKind::Array) {
    g_llvm_trap = true;
    return;
  }
  uint32_t length = Simple::VM::ReadU32Payload(obj->payload, 0);
  int32_t count = Simple::VM::Runtime::UnpackI32(count_slot);
  if (count < 0 || static_cast<uint32_t>(count) > length) {
    g_llvm_trap = true;
    return;
  }
  uint32_t elem_size = (obj->payload.size() == 4u + static_cast<size_t>(length) * 8u) ? 8u : 4u;
  for (uint32_t i = 0; i < static_cast<uint32_t>(count); ++i) {
    size_t offset = 4u + static_cast<size_t>(i) * elem_size;
    if (elem_size == 8) Simple::VM::WriteU64Payload(obj->payload, offset, fill_slot);
    else Simple::VM::WriteU32Payload(obj->payload, offset, static_cast<uint32_t>(fill_slot));
  }
}

extern "C" uint64_t SimpleVmLlvmNewList(uint32_t type_id, uint32_t capacity, uint32_t element_size) {
  if (!g_llvm_heap || element_size == 0) { g_llvm_trap = true; return 0; }
  uint64_t bytes = 8ull + static_cast<uint64_t>(capacity) * static_cast<uint64_t>(element_size);
  if (bytes > static_cast<uint64_t>(std::numeric_limits<uint32_t>::max())) { g_llvm_trap = true; return 0; }
  uint32_t handle = g_llvm_heap->Allocate(Simple::VM::ObjectKind::List, type_id, static_cast<uint32_t>(bytes));
  Simple::VM::HeapObject* obj = g_llvm_heap->Get(handle);
  if (!obj) { g_llvm_trap = true; return 0; }
  Simple::VM::WriteU32Payload(obj->payload, 0, 0);
  Simple::VM::WriteU32Payload(obj->payload, 4, capacity);
  return Simple::VM::Runtime::PackRef(handle);
}

static Simple::VM::HeapObject* SimpleVmLlvmCheckedList(uint64_t ref_slot, uint64_t index_slot,
                                                       uint32_t& length, int32_t& index) {
  if (!g_llvm_heap || Simple::VM::Runtime::IsNullRef(ref_slot)) { g_llvm_trap = true; return nullptr; }
  Simple::VM::HeapObject* obj = g_llvm_heap->Get(Simple::VM::Runtime::UnpackRef(ref_slot));
  if (!obj || obj->header.kind != Simple::VM::ObjectKind::List) { g_llvm_trap = true; return nullptr; }
  length = Simple::VM::ReadU32Payload(obj->payload, 0);
  index = Simple::VM::Runtime::UnpackI32(index_slot);
  if (index < 0 || static_cast<uint32_t>(index) >= length) { g_llvm_trap = true; return nullptr; }
  return obj;
}

extern "C" uint64_t SimpleVmLlvmListLen(uint64_t ref_slot) {
  if (!g_llvm_heap || Simple::VM::Runtime::IsNullRef(ref_slot)) { g_llvm_trap = true; return 0; }
  Simple::VM::HeapObject* obj = g_llvm_heap->Get(Simple::VM::Runtime::UnpackRef(ref_slot));
  if (!obj || obj->header.kind != Simple::VM::ObjectKind::List) { g_llvm_trap = true; return 0; }
  return Simple::VM::Runtime::PackI32(static_cast<int32_t>(Simple::VM::ReadU32Payload(obj->payload, 0)));
}

extern "C" uint64_t SimpleVmLlvmListGet32(uint64_t ref_slot, uint64_t index_slot) {
  uint32_t length = 0; int32_t index = 0;
  Simple::VM::HeapObject* obj = SimpleVmLlvmCheckedList(ref_slot, index_slot, length, index);
  if (!obj) return 0;
  return Simple::VM::ReadU32Payload(obj->payload, 8 + static_cast<size_t>(index) * 4u);
}

extern "C" uint64_t SimpleVmLlvmListGet64(uint64_t ref_slot, uint64_t index_slot) {
  uint32_t length = 0; int32_t index = 0;
  Simple::VM::HeapObject* obj = SimpleVmLlvmCheckedList(ref_slot, index_slot, length, index);
  if (!obj) return 0;
  return Simple::VM::ReadU64Payload(obj->payload, 8 + static_cast<size_t>(index) * 8u);
}

extern "C" void SimpleVmLlvmListSet32(uint64_t ref_slot, uint64_t index_slot, uint64_t value_slot) {
  uint32_t length = 0; int32_t index = 0;
  Simple::VM::HeapObject* obj = SimpleVmLlvmCheckedList(ref_slot, index_slot, length, index);
  if (!obj) return;
  Simple::VM::WriteU32Payload(obj->payload, 8 + static_cast<size_t>(index) * 4u, static_cast<uint32_t>(value_slot));
}

extern "C" void SimpleVmLlvmListSet64(uint64_t ref_slot, uint64_t index_slot, uint64_t value_slot) {
  uint32_t length = 0; int32_t index = 0;
  Simple::VM::HeapObject* obj = SimpleVmLlvmCheckedList(ref_slot, index_slot, length, index);
  if (!obj) return;
  Simple::VM::WriteU64Payload(obj->payload, 8 + static_cast<size_t>(index) * 8u, value_slot);
}

static bool SimpleVmLlvmEnsureListCapacity(Simple::VM::HeapObject* obj, uint32_t min_capacity, uint32_t elem_size) {
  uint32_t capacity = Simple::VM::ReadU32Payload(obj->payload, 4);
  if (capacity >= min_capacity) return true;
  uint32_t new_capacity = capacity == 0 ? 4 : capacity;
  while (new_capacity < min_capacity) {
    if (new_capacity > (std::numeric_limits<uint32_t>::max() / 2u)) return false;
    new_capacity *= 2u;
  }
  uint64_t bytes = 8ull + static_cast<uint64_t>(new_capacity) * static_cast<uint64_t>(elem_size);
  if (bytes > static_cast<uint64_t>(std::numeric_limits<uint32_t>::max())) return false;
  obj->payload.resize(static_cast<size_t>(bytes));
  Simple::VM::WriteU32Payload(obj->payload, 4, new_capacity);
  return true;
}

extern "C" void SimpleVmLlvmListPush32(uint64_t ref_slot, uint64_t value_slot) {
  if (!g_llvm_heap || Simple::VM::Runtime::IsNullRef(ref_slot)) { g_llvm_trap = true; return; }
  Simple::VM::HeapObject* obj = g_llvm_heap->Get(Simple::VM::Runtime::UnpackRef(ref_slot));
  if (!obj || obj->header.kind != Simple::VM::ObjectKind::List) { g_llvm_trap = true; return; }
  uint32_t length = Simple::VM::ReadU32Payload(obj->payload, 0);
  if (!SimpleVmLlvmEnsureListCapacity(obj, length + 1u, 4u)) { g_llvm_trap = true; return; }
  Simple::VM::WriteU32Payload(obj->payload, 8 + static_cast<size_t>(length) * 4u, static_cast<uint32_t>(value_slot));
  Simple::VM::WriteU32Payload(obj->payload, 0, length + 1u);
}

extern "C" void SimpleVmLlvmListPush64(uint64_t ref_slot, uint64_t value_slot) {
  if (!g_llvm_heap || Simple::VM::Runtime::IsNullRef(ref_slot)) { g_llvm_trap = true; return; }
  Simple::VM::HeapObject* obj = g_llvm_heap->Get(Simple::VM::Runtime::UnpackRef(ref_slot));
  if (!obj || obj->header.kind != Simple::VM::ObjectKind::List) { g_llvm_trap = true; return; }
  uint32_t length = Simple::VM::ReadU32Payload(obj->payload, 0);
  if (!SimpleVmLlvmEnsureListCapacity(obj, length + 1u, 8u)) { g_llvm_trap = true; return; }
  Simple::VM::WriteU64Payload(obj->payload, 8 + static_cast<size_t>(length) * 8u, value_slot);
  Simple::VM::WriteU32Payload(obj->payload, 0, length + 1u);
}

extern "C" uint64_t SimpleVmLlvmListPop32(uint64_t ref_slot) {
  if (!g_llvm_heap || Simple::VM::Runtime::IsNullRef(ref_slot)) { g_llvm_trap = true; return 0; }
  Simple::VM::HeapObject* obj = g_llvm_heap->Get(Simple::VM::Runtime::UnpackRef(ref_slot));
  if (!obj || obj->header.kind != Simple::VM::ObjectKind::List) { g_llvm_trap = true; return 0; }
  uint32_t length = Simple::VM::ReadU32Payload(obj->payload, 0);
  if (length == 0) { g_llvm_trap = true; return 0; }
  uint32_t index = length - 1u;
  uint32_t value = Simple::VM::ReadU32Payload(obj->payload, 8 + static_cast<size_t>(index) * 4u);
  Simple::VM::WriteU32Payload(obj->payload, 0, index);
  return value;
}

extern "C" uint64_t SimpleVmLlvmListPop64(uint64_t ref_slot) {
  if (!g_llvm_heap || Simple::VM::Runtime::IsNullRef(ref_slot)) { g_llvm_trap = true; return 0; }
  Simple::VM::HeapObject* obj = g_llvm_heap->Get(Simple::VM::Runtime::UnpackRef(ref_slot));
  if (!obj || obj->header.kind != Simple::VM::ObjectKind::List) { g_llvm_trap = true; return 0; }
  uint32_t length = Simple::VM::ReadU32Payload(obj->payload, 0);
  if (length == 0) { g_llvm_trap = true; return 0; }
  uint32_t index = length - 1u;
  uint64_t value = Simple::VM::ReadU64Payload(obj->payload, 8 + static_cast<size_t>(index) * 8u);
  Simple::VM::WriteU32Payload(obj->payload, 0, index);
  return value;
}

extern "C" void SimpleVmLlvmListInsert32(uint64_t ref_slot, uint64_t index_slot, uint64_t value_slot) {
  if (!g_llvm_heap || Simple::VM::Runtime::IsNullRef(ref_slot)) { g_llvm_trap = true; return; }
  Simple::VM::HeapObject* obj = g_llvm_heap->Get(Simple::VM::Runtime::UnpackRef(ref_slot));
  if (!obj || obj->header.kind != Simple::VM::ObjectKind::List) { g_llvm_trap = true; return; }
  uint32_t length = Simple::VM::ReadU32Payload(obj->payload, 0);
  int32_t index = Simple::VM::Runtime::UnpackI32(index_slot);
  if (index < 0 || static_cast<uint32_t>(index) > length || !SimpleVmLlvmEnsureListCapacity(obj, length + 1u, 4u)) { g_llvm_trap = true; return; }
  size_t at = 8u + static_cast<size_t>(index) * 4u;
  size_t bytes = static_cast<size_t>(length - static_cast<uint32_t>(index)) * 4u;
  std::memmove(obj->payload.data() + at + 4u, obj->payload.data() + at, bytes);
  Simple::VM::WriteU32Payload(obj->payload, at, static_cast<uint32_t>(value_slot));
  Simple::VM::WriteU32Payload(obj->payload, 0, length + 1u);
}

extern "C" void SimpleVmLlvmListInsert64(uint64_t ref_slot, uint64_t index_slot, uint64_t value_slot) {
  if (!g_llvm_heap || Simple::VM::Runtime::IsNullRef(ref_slot)) { g_llvm_trap = true; return; }
  Simple::VM::HeapObject* obj = g_llvm_heap->Get(Simple::VM::Runtime::UnpackRef(ref_slot));
  if (!obj || obj->header.kind != Simple::VM::ObjectKind::List) { g_llvm_trap = true; return; }
  uint32_t length = Simple::VM::ReadU32Payload(obj->payload, 0);
  int32_t index = Simple::VM::Runtime::UnpackI32(index_slot);
  if (index < 0 || static_cast<uint32_t>(index) > length || !SimpleVmLlvmEnsureListCapacity(obj, length + 1u, 8u)) { g_llvm_trap = true; return; }
  size_t at = 8u + static_cast<size_t>(index) * 8u;
  size_t bytes = static_cast<size_t>(length - static_cast<uint32_t>(index)) * 8u;
  std::memmove(obj->payload.data() + at + 8u, obj->payload.data() + at, bytes);
  Simple::VM::WriteU64Payload(obj->payload, at, value_slot);
  Simple::VM::WriteU32Payload(obj->payload, 0, length + 1u);
}

extern "C" uint64_t SimpleVmLlvmListRemove32(uint64_t ref_slot, uint64_t index_slot) {
  uint32_t length = 0; int32_t index = 0;
  Simple::VM::HeapObject* obj = SimpleVmLlvmCheckedList(ref_slot, index_slot, length, index);
  if (!obj) return 0;
  size_t at = 8u + static_cast<size_t>(index) * 4u;
  uint32_t value = Simple::VM::ReadU32Payload(obj->payload, at);
  size_t bytes = static_cast<size_t>(length - static_cast<uint32_t>(index) - 1u) * 4u;
  std::memmove(obj->payload.data() + at, obj->payload.data() + at + 4u, bytes);
  Simple::VM::WriteU32Payload(obj->payload, 0, length - 1u);
  return value;
}

extern "C" uint64_t SimpleVmLlvmListRemove64(uint64_t ref_slot, uint64_t index_slot) {
  uint32_t length = 0; int32_t index = 0;
  Simple::VM::HeapObject* obj = SimpleVmLlvmCheckedList(ref_slot, index_slot, length, index);
  if (!obj) return 0;
  size_t at = 8u + static_cast<size_t>(index) * 8u;
  uint64_t value = Simple::VM::ReadU64Payload(obj->payload, at);
  size_t bytes = static_cast<size_t>(length - static_cast<uint32_t>(index) - 1u) * 8u;
  std::memmove(obj->payload.data() + at, obj->payload.data() + at + 8u, bytes);
  Simple::VM::WriteU32Payload(obj->payload, 0, length - 1u);
  return value;
}

static uint32_t SimpleVmLlvmListElemSize(Simple::VM::HeapObject* obj) {
  if (!g_llvm_module || obj->header.type_id >= g_llvm_module->types.size()) return 4u;
  Simple::Byte::TypeKind kind = static_cast<Simple::Byte::TypeKind>(g_llvm_module->types[obj->header.type_id].kind);
  return (kind == Simple::Byte::TypeKind::I64 || kind == Simple::Byte::TypeKind::U64 || kind == Simple::Byte::TypeKind::F64) ? 8u : 4u;
}

extern "C" void SimpleVmLlvmListClear(uint64_t ref_slot) {
  if (!g_llvm_heap || Simple::VM::Runtime::IsNullRef(ref_slot)) { g_llvm_trap = true; return; }
  Simple::VM::HeapObject* obj = g_llvm_heap->Get(Simple::VM::Runtime::UnpackRef(ref_slot));
  if (!obj || obj->header.kind != Simple::VM::ObjectKind::List) { g_llvm_trap = true; return; }
  Simple::VM::WriteU32Payload(obj->payload, 0, 0);
}

extern "C" void SimpleVmLlvmListReserve(uint64_t ref_slot, uint64_t capacity_slot) {
  if (!g_llvm_heap || Simple::VM::Runtime::IsNullRef(ref_slot)) { g_llvm_trap = true; return; }
  Simple::VM::HeapObject* obj = g_llvm_heap->Get(Simple::VM::Runtime::UnpackRef(ref_slot));
  if (!obj || obj->header.kind != Simple::VM::ObjectKind::List) { g_llvm_trap = true; return; }
  int32_t requested = Simple::VM::Runtime::UnpackI32(capacity_slot);
  if (requested < 0 || !SimpleVmLlvmEnsureListCapacity(obj, static_cast<uint32_t>(requested), SimpleVmLlvmListElemSize(obj))) {
    g_llvm_trap = true;
  }
}

extern "C" void SimpleVmLlvmListResize(uint64_t ref_slot, uint64_t size_slot, uint64_t fill_slot) {
  if (!g_llvm_heap || Simple::VM::Runtime::IsNullRef(ref_slot)) { g_llvm_trap = true; return; }
  Simple::VM::HeapObject* obj = g_llvm_heap->Get(Simple::VM::Runtime::UnpackRef(ref_slot));
  if (!obj || obj->header.kind != Simple::VM::ObjectKind::List) { g_llvm_trap = true; return; }
  int32_t requested = Simple::VM::Runtime::UnpackI32(size_slot);
  if (requested < 0) { g_llvm_trap = true; return; }
  uint32_t new_length = static_cast<uint32_t>(requested);
  uint32_t elem_size = SimpleVmLlvmListElemSize(obj);
  uint32_t old_length = Simple::VM::ReadU32Payload(obj->payload, 0);
  if (!SimpleVmLlvmEnsureListCapacity(obj, new_length, elem_size)) { g_llvm_trap = true; return; }
  for (uint32_t i = old_length; i < new_length; ++i) {
    size_t offset = 8u + static_cast<size_t>(i) * elem_size;
    if (elem_size == 8u) Simple::VM::WriteU64Payload(obj->payload, offset, fill_slot);
    else Simple::VM::WriteU32Payload(obj->payload, offset, static_cast<uint32_t>(fill_slot));
  }
  Simple::VM::WriteU32Payload(obj->payload, 0, new_length);
}

extern "C" uint64_t SimpleVmLlvmCallDynamicDl(const Simple::Byte::SbcModule* module,
                                              uint32_t func_index,
                                              uint64_t* args,
                                              uint32_t argc,
                                              uint64_t* caller_locals,
                                              uint32_t caller_local_count,
                                              uint64_t caller_local_ref_mask,
                                              uint64_t* caller_stack,
                                              uint32_t caller_stack_count,
                                              uint64_t caller_stack_ref_mask,
                                              uint32_t caller_func_index,
                                              uint32_t caller_pc,
                                              uint8_t* has_ret) {
  if (has_ret) *has_ret = 0;
  Simple::VM::Jit::JitCallContext context;
  context.args.reserve(argc);
  for (uint32_t i = 0; i < argc; ++i) context.args.push_back(args ? args[i] : 0);
  context.locals.reserve(caller_local_count);
  for (uint32_t i = 0; i < caller_local_count; ++i) context.locals.push_back(caller_locals ? caller_locals[i] : 0);
  context.operand_stack.reserve(caller_stack_count);
  for (uint32_t i = 0; i < caller_stack_count; ++i) context.operand_stack.push_back(caller_stack ? caller_stack[i] : 0);
  context.heap = g_llvm_heap;
  context.globals = g_llvm_globals;
  if (!module || !context.heap || func_index >= module->functions.size()) {
    Simple::VM::Jit::SetJitTrap(&context, Simple::VM::Jit::JitCallTrapKind::Trap,
                                "LLVM JIT dynamic dl helper invalid function metadata");
    g_llvm_trap = true;
    return 0;
  }
  const auto& func = module->functions[func_index];
  if (func.method_id >= module->methods.size()) {
    g_llvm_trap = true;
    return 0;
  }
  const auto& method = module->methods[func.method_id];
  if (method.sig_id >= module->sigs.size()) {
    g_llvm_trap = true;
    return 0;
  }
  const auto& sig = module->sigs[method.sig_id];
  if (argc != sig.param_count || sig.param_count == 0 ||
      sig.param_type_start + sig.param_count > module->param_types.size()) {
    Simple::VM::Jit::SetJitTrap(&context, Simple::VM::Jit::JitCallTrapKind::Trap,
                                "LLVM JIT dynamic dl helper argument metadata mismatch");
    g_llvm_trap = true;
    return 0;
  }
  const uint32_t ptr_type_id = module->param_types[sig.param_type_start];
  if (ptr_type_id >= module->types.size()) {
    g_llvm_trap = true;
    return 0;
  }
  const auto ptr_kind = static_cast<Simple::Byte::TypeKind>(module->types[ptr_type_id].kind);
  if (ptr_kind != Simple::Byte::TypeKind::I64 && ptr_kind != Simple::Byte::TypeKind::U64) {
    g_llvm_trap = true;
    return 0;
  }
  const int64_t ptr_bits = Simple::VM::Runtime::UnpackI64(context.args[0]);
  if (ptr_bits == 0) {
    g_llvm_trap = true;
    return 0;
  }
  std::vector<uint32_t> all_arg_type_ids;
  all_arg_type_ids.reserve(sig.param_count);
  std::vector<uint32_t> ffi_arg_type_ids;
  ffi_arg_type_ids.reserve(argc - 1u);
  for (uint16_t i = 0; i < sig.param_count; ++i) {
    const uint32_t type_id = module->param_types[sig.param_type_start + i];
    if (type_id >= module->types.size()) {
      g_llvm_trap = true;
      return 0;
    }
    all_arg_type_ids.push_back(type_id);
    if (i != 0) ffi_arg_type_ids.push_back(type_id);
  }
  if (!Simple::VM::Jit::PublishJitRootsFromContext(&context, *module, all_arg_type_ids, {}, {})) {
    g_llvm_trap = true;
    return 0;
  }
  Simple::VM::Jit::PublishJitRootSlotsByMask(&context, context.locals, caller_local_ref_mask);
  Simple::VM::Jit::PublishJitRootSlotsByMask(&context, context.operand_stack, caller_stack_ref_mask);
  Simple::VM::Jit::MarkJitSafepoint(&context, caller_func_index, caller_pc, true, false);
  struct JitRootFrameScope {
    const std::vector<uint32_t>* roots = nullptr;
    explicit JitRootFrameScope(const std::vector<uint32_t>* refs) : roots(refs) {
      Simple::VM::Jit::PushJitRootFrame(roots);
    }
    ~JitRootFrameScope() { Simple::VM::Jit::PopJitRootFrame(roots); }
  } jit_root_frame_scope(&context.root_refs);
  Slot ret = 0;
  std::string error;
  const bool ret_present = !LlvmTypeIdIsVoidLike(*module, sig.ret_type_id);
  if (!Simple::VM::Ffi::DispatchDynamicDlCall(ptr_bits, *module, sig.ret_type_id, ret_present,
                                             ffi_arg_type_ids, context.args, 1, *context.heap, &ret, &error)) {
    g_llvm_dl_last_error = error;
    Simple::VM::Jit::SetJitTrap(&context, Simple::VM::Jit::JitCallTrapKind::Trap, error);
    g_llvm_trap = true;
    return 0;
  }
  g_llvm_dl_last_error.clear();
  if (ret_present) Simple::VM::Jit::SetJitReturn(&context, ret);
  else Simple::VM::Jit::ClearJitReturn(&context);
  if (has_ret) *has_ret = context.has_return ? 1 : 0;
  return context.return_value;
}

extern "C" uint64_t SimpleVmLlvmCallFunction(const Simple::Byte::SbcModule* module,
                                             uint32_t func_index,
                                             uint64_t* args,
                                             uint32_t argc,
                                             uint64_t* caller_locals,
                                             uint32_t caller_local_count,
                                             uint64_t caller_local_ref_mask,
                                             uint64_t* caller_stack,
                                             uint32_t caller_stack_count,
                                             uint64_t caller_stack_ref_mask,
                                             uint32_t caller_func_index,
                                             uint32_t caller_pc,
                                             uint8_t may_block,
                                             uint8_t may_allocate,
                                             uint8_t* has_ret) {
  if (has_ret) *has_ret = 0;
  if (!module) return 0;
  Simple::VM::Jit::JitCallContext context;
  context.args.reserve(argc);
  for (uint32_t i = 0; i < argc; ++i) context.args.push_back(args ? args[i] : 0);
  context.locals.reserve(caller_local_count);
  for (uint32_t i = 0; i < caller_local_count; ++i) context.locals.push_back(caller_locals ? caller_locals[i] : 0);
  context.operand_stack.reserve(caller_stack_count);
  for (uint32_t i = 0; i < caller_stack_count; ++i) context.operand_stack.push_back(caller_stack ? caller_stack[i] : 0);
  context.heap = g_llvm_heap;
  context.globals = g_llvm_globals;

  std::vector<uint32_t> arg_type_ids;
  if (func_index >= module->functions.size() || module->functions[func_index].method_id >= module->methods.size()) {
    Simple::VM::Jit::SetJitTrap(&context, Simple::VM::Jit::JitCallTrapKind::Trap, "LLVM JIT helper invalid function metadata");
    g_llvm_trap = true;
    return 0;
  }
  const auto& helper_method = module->methods[module->functions[func_index].method_id];
  if (helper_method.sig_id >= module->sigs.size()) {
    Simple::VM::Jit::SetJitTrap(&context, Simple::VM::Jit::JitCallTrapKind::Trap, "LLVM JIT helper invalid signature metadata");
    g_llvm_trap = true;
    return 0;
  }
  const auto& helper_sig = module->sigs[helper_method.sig_id];
  if (helper_sig.param_type_start + helper_sig.param_count > module->param_types.size() ||
      helper_sig.param_count != argc) {
    Simple::VM::Jit::SetJitTrap(&context, Simple::VM::Jit::JitCallTrapKind::Trap, "LLVM JIT helper argument metadata mismatch");
    g_llvm_trap = true;
    return 0;
  }
  arg_type_ids.reserve(helper_sig.param_count);
  for (uint16_t i = 0; i < helper_sig.param_count; ++i) {
    arg_type_ids.push_back(module->param_types[helper_sig.param_type_start + i]);
  }
  if (!Simple::VM::Jit::PublishJitRootsFromContext(&context, *module, arg_type_ids, {}, {})) {
    Simple::VM::Jit::SetJitTrap(&context, Simple::VM::Jit::JitCallTrapKind::Trap, "LLVM JIT helper root publication failed");
    g_llvm_trap = true;
    return 0;
  }
  Simple::VM::Jit::PublishJitRootSlotsByMask(&context, context.locals, caller_local_ref_mask);
  Simple::VM::Jit::PublishJitRootSlotsByMask(&context, context.operand_stack, caller_stack_ref_mask);
  Simple::VM::Jit::MarkJitSafepoint(&context, caller_func_index, caller_pc, may_block != 0, may_allocate != 0);
  struct JitRootFrameScope {
    const std::vector<uint32_t>* roots = nullptr;
    explicit JitRootFrameScope(const std::vector<uint32_t>* refs) : roots(refs) {
      Simple::VM::Jit::PushJitRootFrame(roots);
    }
    ~JitRootFrameScope() { Simple::VM::Jit::PopJitRootFrame(roots); }
  } jit_root_frame_scope(&context.root_refs);

  std::string reason;
  if (func_index < module->function_is_import.size() && module->function_is_import[func_index]) {
    if (!context.heap) {
      Simple::VM::Jit::SetJitTrap(&context, Simple::VM::Jit::JitCallTrapKind::Trap, "LLVM JIT import missing heap");
      g_llvm_trap = true;
      return 0;
    }
    static Simple::VM::Native::NativeRegistry* registry = new Simple::VM::Native::NativeRegistry(Simple::VM::Native::BuildDefaultRegistry());
    Simple::VM::ExecOptions default_options;
    const Simple::VM::ExecOptions& options = g_llvm_exec_options ? *g_llvm_exec_options : default_options;
    Slot ret = 0;
    bool ret_present = false;
    std::string error;
    if (!Simple::VM::Runtime::DispatchImportCallByName(*module, options, *registry, *context.heap,
                                                       g_llvm_file_handles, g_llvm_resource_registry,
                                                       g_llvm_dl_last_error,
                                                       func_index, context.args, ret, ret_present, error)) {
      Simple::VM::Jit::SetJitTrap(&context, Simple::VM::Jit::JitCallTrapKind::Trap, error);
      g_llvm_trap = true;
      return 0;
    }
    if (ret_present) Simple::VM::Jit::SetJitReturn(&context, ret);
    else Simple::VM::Jit::ClearJitReturn(&context);
  } else {
    Simple::VM::Jit::LlvmJitBackend backend;
    Slot ret = 0;
    bool ret_present = false;
    if (!backend.TryRunFunctionWithRuntime(*module, func_index, context.args, context.heap, context.globals,
                                           g_llvm_exec_options, ret, ret_present, reason)) {
      Simple::VM::Jit::SetJitTrap(&context, Simple::VM::Jit::JitCallTrapKind::Trap, reason);
      g_llvm_trap = true;
      return 0;
    }
    if (ret_present) Simple::VM::Jit::SetJitReturn(&context, ret);
    else Simple::VM::Jit::ClearJitReturn(&context);
  }
  if (has_ret) *has_ret = context.has_return ? 1 : 0;
  return context.return_value;
}

std::string ToString(llvm::Error error) {
  std::string message;
  llvm::raw_string_ostream os(message);
  llvm::logAllUnhandledErrors(std::move(error), os, "");
  return os.str();
}

uint64_t HashFunctionCode(const Simple::Byte::SbcModule& module, const Simple::Byte::FunctionRow& func) {
  uint64_t hash = 1469598103934665603ull;
  const size_t end = func.code_offset + func.code_size;
  for (size_t i = func.code_offset; i < end; ++i) {
    hash ^= static_cast<uint64_t>(module.code[i]);
    hash *= 1099511628211ull;
  }
  return hash;
}

bool RunCachedEntry(const std::shared_ptr<CachedLlvmEntry>& cached,
                    const Simple::Byte::SbcModule& module,
                    const std::vector<Slot>& args,
                    Simple::VM::Heap* heap,
                    std::vector<Slot>* globals_ptr,
                    const Simple::VM::ExecOptions* exec_options,
                    Slot& out_ret,
                    bool& out_has_ret,
                    std::string& reason) {
  uint64_t* raw_args = args.empty() ? nullptr : const_cast<uint64_t*>(args.data());
  Simple::VM::Heap* prev_heap = g_llvm_heap;
  std::vector<Slot>* prev_globals = g_llvm_globals;
  const Simple::Byte::SbcModule* prev_module = g_llvm_module;
  const Simple::VM::ExecOptions* prev_options = g_llvm_exec_options;
  g_llvm_heap = heap;
  g_llvm_globals = globals_ptr;
  g_llvm_module = &module;
  g_llvm_exec_options = exec_options;
  g_llvm_trap = false;
  out_ret = cached->entry(raw_args, static_cast<uint32_t>(args.size()));
  g_llvm_heap = prev_heap;
  g_llvm_globals = prev_globals;
  g_llvm_module = prev_module;
  g_llvm_exec_options = prev_options;
  if (g_llvm_trap) {
    g_llvm_trap = false;
    reason = "unsupported";
    return false;
  }
  out_has_ret = cached->has_ret;
  return true;
}

#endif

} // namespace

std::string BuildLlvmJitCacheKey(uintptr_t module_identity,
                                 size_t function_index,
                                 uint32_t code_offset,
                                 uint32_t code_size,
                                 uint64_t code_hash,
                                 bool uses_runtime_helpers) {
  return std::to_string(module_identity) + ":" +
         std::to_string(function_index) + ":" +
         std::to_string(code_offset) + ":" +
         std::to_string(code_size) + ":" +
         std::to_string(code_hash) + ":jitabi=" +
         std::to_string(kLlvmJitCacheAbiVersion) + ":helperabi=" +
         std::to_string(kLlvmJitRuntimeHelperAbiVersion) + ":" +
         (uses_runtime_helpers ? "rt" : "standalone");
}

LlvmJitBackend::LlvmJitBackend(LlvmJitOptions options) : options_(options) {
#if defined(SIMPLEVM_HAS_LLVM)
  static const bool initialized = []() {
    llvm::InitializeNativeTarget();
    llvm::InitializeNativeTargetAsmPrinter();
    llvm::InitializeNativeTargetAsmParser();
    return true;
  }();
  (void)initialized;
#endif
}

LlvmJitStatus GetLlvmJitStatus() {
#if defined(SIMPLEVM_HAS_LLVM)
  return {true, "LLVM ORC JIT support compiled in"};
#else
  return {false, "LLVM ORC JIT support not compiled in; configure with -DSIMPLEVM_ENABLE_LLVM_JIT=ON"};
#endif
}

LlvmJitStatus LlvmJitBackend::Status() const {
  (void)options_;
  return GetLlvmJitStatus();
}

bool LlvmJitBackend::CanAcceptFunction(const Simple::Byte::SbcModule& module,
                                       size_t func_index,
                                       std::string& reason) const {
  LlvmJitStatus status = Status();
  if (!status.available) {
    reason = status.message;
    return false;
  }
  if (func_index >= module.functions.size()) {
    reason = "function index out of range";
    return false;
  }
  const auto& func = module.functions[func_index];
  if (func.code_offset + func.code_size > module.code.size()) {
    reason = "function code range out of bounds";
    return false;
  }
  reason.clear();
  return true;
}

bool LlvmJitBackend::TryRunFunction(const Simple::Byte::SbcModule& module,
                                    size_t func_index,
                                    const std::vector<Slot>& args,
                                    Slot& out_ret,
                                    bool& out_has_ret,
                                    std::string& reason) const {
  return TryRunFunctionWithRuntime(module, func_index, args, nullptr, nullptr, nullptr, out_ret, out_has_ret, reason);
}

bool LlvmJitBackend::TryRunFunctionWithRuntime(const Simple::Byte::SbcModule& module,
                                               size_t func_index,
                                               const std::vector<Slot>& args,
                                               Simple::VM::Heap* heap,
                                               std::vector<Slot>* globals_ptr,
                                               const Simple::VM::ExecOptions* exec_options,
                                               Slot& out_ret,
                                               bool& out_has_ret,
                                               std::string& reason) const {
#if !defined(SIMPLEVM_HAS_LLVM)
  (void)module;
  (void)func_index;
  (void)args;
  (void)heap;
  (void)globals_ptr;
  (void)exec_options;
  (void)out_ret;
  (void)out_has_ret;
  reason = "unsupported";
  return false;
#else
  reason.clear();
  if (!CanAcceptFunction(module, func_index, reason)) return false;

  const auto& func = module.functions[func_index];
  if (func.method_id >= module.methods.size()) {
    reason = "LLVM JIT invalid method id";
    return false;
  }
  const auto& method = module.methods[func.method_id];
  if (method.sig_id >= module.sigs.size()) {
    reason = "LLVM JIT invalid signature id";
    return false;
  }
  const auto& sig = module.sigs[method.sig_id];
  auto sig_returns_void = [&](const Simple::Byte::SigRow& row) -> bool {
    return LlvmTypeIdIsVoidLike(module, row.ret_type_id);
  };
  auto type_is_scalar_loop_call_safe = [&](uint32_t type_id) -> bool {
    if (type_id == 0xFFFFFFFFu) return true;
    if (type_id >= module.types.size()) return false;
    const auto& row = module.types[type_id];
    if (Simple::Byte::IsManagedArtifactType(row) || Simple::Byte::IsOpaqueHandleType(row)) return false;
    switch (static_cast<Simple::Byte::TypeKind>(row.kind)) {
      case Simple::Byte::TypeKind::Unspecified:
      case Simple::Byte::TypeKind::Void:
      case Simple::Byte::TypeKind::I8:
      case Simple::Byte::TypeKind::I16:
      case Simple::Byte::TypeKind::I32:
      case Simple::Byte::TypeKind::I64:
      case Simple::Byte::TypeKind::U8:
      case Simple::Byte::TypeKind::U16:
      case Simple::Byte::TypeKind::U32:
      case Simple::Byte::TypeKind::U64:
      case Simple::Byte::TypeKind::F32:
      case Simple::Byte::TypeKind::F64:
      case Simple::Byte::TypeKind::Bool:
      case Simple::Byte::TypeKind::Char:
      case Simple::Byte::TypeKind::Ptr:
        return true;
      default:
        return false;
    }
  };
  auto sig_is_scalar_loop_call_safe = [&](const Simple::Byte::SigRow& row) -> bool {
    if (!type_is_scalar_loop_call_safe(row.ret_type_id)) return false;
    if (row.param_type_start + row.param_count > module.param_types.size()) return false;
    for (uint16_t i = 0; i < row.param_count; ++i) {
      if (!type_is_scalar_loop_call_safe(module.param_types[row.param_type_start + i])) return false;
    }
    return true;
  };
  auto signature_type_kinds = [&](const Simple::Byte::SigRow& row,
                                  std::vector<Simple::Byte::TypeKind>* params,
                                  Simple::Byte::TypeKind* result) -> bool {
    if ((row.ret_type_id != 0xFFFFFFFFu && row.ret_type_id >= module.types.size()) ||
        row.param_type_start + row.param_count > module.param_types.size()) {
      return false;
    }
    if (result) {
      *result = row.ret_type_id == 0xFFFFFFFFu
                    ? Simple::Byte::TypeKind::Unspecified
                    : static_cast<Simple::Byte::TypeKind>(module.types[row.ret_type_id].kind);
    }
    if (params) {
      params->clear();
      params->reserve(row.param_count);
      for (uint16_t i = 0; i < row.param_count; ++i) {
        const uint32_t type_id = module.param_types[row.param_type_start + i];
        if (type_id >= module.types.size()) return false;
        params->push_back(static_cast<Simple::Byte::TypeKind>(module.types[type_id].kind));
      }
    }
    return true;
  };
  auto dl_call_loop_safe = [&](const Simple::Byte::SigRow& row) -> bool {
    if (row.param_count == 0 || row.param_type_start + row.param_count > module.param_types.size()) return false;
    std::vector<uint32_t> param_type_ids;
    param_type_ids.reserve(row.param_count);
    for (uint16_t i = 0; i < row.param_count; ++i) {
      param_type_ids.push_back(module.param_types[row.param_type_start + i]);
    }
    const auto abi = Simple::VM::Ffi::AnalyzeDynamicDlFunctionSignature(module,
                                                                        row.ret_type_id,
                                                                        !sig_returns_void(row),
                                                                        param_type_ids);
    return abi.jit_loop_safe;
  };
  auto native_metadata_matches_signature = [&](const Simple::VM::Native::NativeFunctionSpec& spec,
                                               const Simple::Byte::SigRow& row) -> bool {
    std::vector<Simple::Byte::TypeKind> params;
    Simple::Byte::TypeKind result = Simple::Byte::TypeKind::Unspecified;
    if (!signature_type_kinds(row, &params, &result)) return false;
    if (spec.parameter_types.size() != params.size()) return false;
    for (size_t i = 0; i < params.size(); ++i) {
      if (spec.parameter_types[i] != params[i]) return false;
    }
    return spec.result_type == Simple::Byte::TypeKind::Unspecified || spec.result_type == result;
  };
  auto function_name = [&](uint32_t func_id) -> std::string {
    if (func_id >= module.functions.size()) return {};
    const uint32_t method_id = module.functions[func_id].method_id;
    if (method_id >= module.methods.size()) return {};
    const uint32_t name_offset = module.methods[method_id].name_str;
    if (name_offset == 0 || name_offset >= module.const_pool.size()) return {};
    return Simple::Byte::ReadConstPoolString(module, name_offset);
  };
  auto import_name = [&](uint32_t func_id, std::string* module_name, std::string* symbol_name) -> bool {
    if (module.imports.empty() || func_id >= module.function_is_import.size() || !module.function_is_import[func_id]) {
      return false;
    }
    const size_t import_base = module.functions.size() - module.imports.size();
    if (func_id < import_base) return false;
    const size_t import_index = func_id - import_base;
    if (import_index >= module.imports.size()) return false;
    const auto& import_row = module.imports[import_index];
    if (module_name) *module_name = Simple::Byte::ReadConstPoolString(module, import_row.module_name_str);
    if (symbol_name) *symbol_name = Simple::Byte::ReadConstPoolString(module, import_row.symbol_name_str);
    return true;
  };
  auto call_target_label = [&](uint32_t func_id, bool import_like_call) -> std::string {
    std::string module_name;
    std::string symbol_name;
    if (import_like_call && import_name(func_id, &module_name, &symbol_name) &&
        !module_name.empty() && !symbol_name.empty()) {
      return module_name + "." + symbol_name;
    }
    std::string name = function_name(func_id);
    if (!name.empty()) return name;
    return "func#" + std::to_string(func_id);
  };
  auto type_kind_label = [](Simple::Byte::TypeKind kind) -> std::string {
    switch (kind) {
      case Simple::Byte::TypeKind::Unspecified: return "unspecified";
      case Simple::Byte::TypeKind::I32: return "i32";
      case Simple::Byte::TypeKind::I64: return "i64";
      case Simple::Byte::TypeKind::F32: return "f32";
      case Simple::Byte::TypeKind::F64: return "f64";
      case Simple::Byte::TypeKind::Ref: return "ref";
      case Simple::Byte::TypeKind::I8: return "i8";
      case Simple::Byte::TypeKind::I16: return "i16";
      case Simple::Byte::TypeKind::I128: return "i128";
      case Simple::Byte::TypeKind::U8: return "u8";
      case Simple::Byte::TypeKind::U16: return "u16";
      case Simple::Byte::TypeKind::U32: return "u32";
      case Simple::Byte::TypeKind::U64: return "u64";
      case Simple::Byte::TypeKind::U128: return "u128";
      case Simple::Byte::TypeKind::Bool: return "bool";
      case Simple::Byte::TypeKind::Char: return "char";
      case Simple::Byte::TypeKind::String: return "string";
      case Simple::Byte::TypeKind::Void: return "void";
      case Simple::Byte::TypeKind::Never: return "never";
      case Simple::Byte::TypeKind::Ptr: return "ptr";
      case Simple::Byte::TypeKind::Array: return "array";
      case Simple::Byte::TypeKind::List: return "list";
      case Simple::Byte::TypeKind::Function: return "function";
      case Simple::Byte::TypeKind::Result: return "result";
      case Simple::Byte::TypeKind::Option: return "option";
      case Simple::Byte::TypeKind::Vector: return "vector";
    }
    return "kind#" + std::to_string(static_cast<uint8_t>(kind));
  };
  auto type_label = [&](uint32_t type_id) -> std::string {
    if (type_id == 0xFFFFFFFFu) return "unspecified";
    if (type_id >= module.types.size()) return "type#" + std::to_string(type_id);
    const auto& row = module.types[type_id];
    std::string name = Simple::Byte::ReadConstPoolString(module, row.name_str);
    if (name.empty()) name = type_kind_label(static_cast<Simple::Byte::TypeKind>(row.kind));
    if (row.field_count == 0 || row.field_start + row.field_count > module.fields.size()) return name;
    std::ostringstream out;
    out << name << "{";
    for (uint32_t i = 0; i < row.field_count; ++i) {
      if (i != 0) out << ",";
      const auto& field = module.fields[row.field_start + i];
      out << (field.type_id < module.types.size()
                  ? type_kind_label(static_cast<Simple::Byte::TypeKind>(module.types[field.type_id].kind))
                  : "type#" + std::to_string(field.type_id))
          << "@" << field.offset;
    }
    out << "}";
    return out.str();
  };
  auto signature_label = [&](const Simple::Byte::SigRow& row) -> std::string {
    std::ostringstream out;
    out << "sig=(";
    for (uint16_t i = 0; i < row.param_count; ++i) {
      if (i != 0) out << ",";
      const uint32_t param_index = row.param_type_start + i;
      out << (param_index < module.param_types.size() ? type_label(module.param_types[param_index]) : "type#out-of-range");
    }
    out << ")->" << type_label(row.ret_type_id);
    return out.str();
  };
  auto native_import_spec = [&](uint32_t func_id) -> const Simple::VM::Native::NativeFunctionSpec* {
    std::string module_name;
    std::string symbol_name;
    if (!import_name(func_id, &module_name, &symbol_name) || module_name.empty() || symbol_name.empty()) return nullptr;
    static const Simple::VM::Native::NativeRegistry* registry =
        new Simple::VM::Native::NativeRegistry(Simple::VM::Native::BuildDefaultRegistry());
    return registry->Find(module_name, symbol_name);
  };
  auto helper_call_safepoint_flags = [&](uint32_t func_id) -> std::pair<bool, bool> {
    if (func_id >= module.function_is_import.size() || !module.function_is_import[func_id]) {
      return {false, true};
    }
    std::string module_name;
    std::string symbol_name;
    if (!import_name(func_id, &module_name, &symbol_name)) return {true, true};
    if (module_name == "System.dl" && symbol_name.rfind("call$", 0) == 0) return {true, false};
    const auto* spec = native_import_spec(func_id);
    if (!spec) return {true, true};
    const bool may_block = spec->blocking != Simple::VM::Native::NativeBlockingBehavior::NonBlocking;
    const bool may_allocate = spec->allocation != Simple::VM::Native::NativeAllocationBehavior::NoAllocation;
    return {may_block, may_allocate};
  };
  auto native_resources_loop_safe = [](const Simple::VM::Native::NativeFunctionSpec& spec) -> bool {
    for (const auto& resource : spec.resources) {
      if (resource.access != Simple::VM::Native::NativeResourceAccess::Input) return false;
      if (resource.parameter_index >= spec.parameter_types.size()) return false;
    }
    return true;
  };
  auto describe_import_loop_call_safety = [&](uint32_t func_id, const Simple::Byte::SigRow& row) -> std::string {
    auto unsafe = [&](const std::string& category, const std::string& why, const std::string& target) {
      return "category=" + category + " reason=" + why + (target.empty() ? std::string() : " target=" + target) +
             " " + signature_label(row);
    };
    const std::string target = call_target_label(func_id, true);
    std::string module_name;
    std::string symbol_name;
    if (!import_name(func_id, &module_name, &symbol_name)) return unsafe("native/import", "missing-import-metadata", target);
    if (module_name.empty() || symbol_name.empty()) return unsafe("native/import", "missing-import-name", target);
    if (module_name == "System.dl" && symbol_name.rfind("call$", 0) == 0) {
      if (dl_call_loop_safe(row)) return std::string();
      if (!sig_is_scalar_loop_call_safe(row)) {
        return unsafe("dynamic-dl/external-c", "non-scalar-or-managed-signature", target);
      }
      return unsafe("dynamic-dl/external-c", "invalid-abi-signature", target);
    }
    const auto* spec = native_import_spec(func_id);
    if (!spec) {
      if (!sig_is_scalar_loop_call_safe(row)) return unsafe("native/import", "non-scalar-or-managed-signature", target);
      return unsafe("native-registry", "missing-native-metadata", target);
    }
    if (!native_metadata_matches_signature(*spec, row)) return unsafe("native-registry", "metadata-signature-mismatch", target);
    if (!native_resources_loop_safe(*spec)) return unsafe("native-registry", "resource-argument-or-result", target);
    if (spec->blocking != Simple::VM::Native::NativeBlockingBehavior::NonBlocking) return unsafe("native-registry", "blocking-call", target);
    if (spec->allocation != Simple::VM::Native::NativeAllocationBehavior::NoAllocation) return unsafe("native-registry", "allocating-call", target);
    if (spec->gc_behavior != Simple::VM::Native::NativeGcBehavior::NoSafepoint) return unsafe("native-registry", "gc-safepoint-call", target);
    return std::string();
  };
  const uint16_t param_count = sig.param_count;
  if (args.size() != param_count) {
    reason = "LLVM JIT arg count mismatch";
    return false;
  }

  const std::string cache_key = BuildLlvmJitCacheKey(reinterpret_cast<uintptr_t>(&module),
                                                     func_index,
                                                     func.code_offset,
                                                     func.code_size,
                                                     HashFunctionCode(module, func),
                                                     globals_ptr != nullptr);
  {
    std::shared_ptr<CachedLlvmEntry> cached;
    {
      std::lock_guard<std::mutex> lock(LlvmCacheMutex());
      auto& cache = LlvmCache();
      auto it = cache.find(cache_key);
      if (it != cache.end()) cached = it->second;
    }
    if (cached) return RunCachedEntry(cached, module, args, heap, globals_ptr, exec_options, out_ret, out_has_ret, reason);
    {
      std::lock_guard<std::mutex> lock(LlvmCacheMutex());
      auto& rejects = LlvmRejectCache();
      auto it = rejects.find(cache_key);
      if (it != rejects.end()) {
        reason = it->second;
        return false;
      }
    }
  }
  auto reject_cached = [&](const std::string& why) -> bool {
    reason = why;
    std::lock_guard<std::mutex> lock(LlvmCacheMutex());
    LlvmRejectCache()[cache_key] = why;
    return false;
  };

  size_t scan_pc = func.code_offset;
  const size_t end_pc = func.code_offset + func.code_size;

  struct JmpTableInfo {
    uint32_t count = 0;
    std::vector<int32_t> rels;
  };
  auto read_const_u32 = [&](size_t offset) -> uint32_t {
    return static_cast<uint32_t>(module.const_pool[offset]) |
           (static_cast<uint32_t>(module.const_pool[offset + 1]) << 8) |
           (static_cast<uint32_t>(module.const_pool[offset + 2]) << 16) |
           (static_cast<uint32_t>(module.const_pool[offset + 3]) << 24);
  };
  auto read_const_u64 = [&](size_t offset) -> uint64_t {
    return static_cast<uint64_t>(read_const_u32(offset)) |
           (static_cast<uint64_t>(read_const_u32(offset + 4)) << 32);
  };
  auto parse_jmp_table = [&](uint32_t const_id, JmpTableInfo& info, std::string& error) -> bool {
    if (const_id + 8 > module.const_pool.size()) {
      error = "LLVM JIT JMP_TABLE const id bad";
      return false;
    }
    uint32_t kind = read_const_u32(const_id);
    if (kind != 6) {
      error = "LLVM JIT JMP_TABLE const kind mismatch";
      return false;
    }
    uint32_t payload = read_const_u32(const_id + 4);
    if (payload + 4 > module.const_pool.size()) {
      error = "LLVM JIT JMP_TABLE blob out of bounds";
      return false;
    }
    uint32_t blob_len = read_const_u32(payload);
    if (payload + 4 + blob_len > module.const_pool.size()) {
      error = "LLVM JIT JMP_TABLE blob out of bounds";
      return false;
    }
    if (blob_len < 4 || (blob_len - 4) % 4 != 0) {
      error = "LLVM JIT JMP_TABLE blob size invalid";
      return false;
    }
    uint32_t count = read_const_u32(payload + 4);
    if (blob_len != 4 + count * 4) {
      error = "LLVM JIT JMP_TABLE blob size mismatch";
      return false;
    }
    info.count = count;
    info.rels.clear();
    info.rels.reserve(count);
    for (uint32_t i = 0; i < count; ++i) {
      uint32_t raw = read_const_u32(payload + 8 + static_cast<size_t>(i) * 4u);
      info.rels.push_back(static_cast<int32_t>(raw));
    }
    return true;
  };

  // Current native subset: leaf i32/bool stack expressions with Enter,
  // ConstI32/ConstBool, arithmetic, comparisons, bool ops, LoadLocal,
  // StoreLocal, Pop, and Ret. This is intentionally small but is real
  // ORC-generated native code and establishes the lowering/execution path.
  bool saw_enter = false;
  uint16_t local_count = 0;
  bool saw_ret = false;
  bool saw_call = false;
  struct UnsafeLoopCallCandidate {
    size_t pc = 0;
    OpCode op = OpCode::Nop;
    std::string detail;
  };
  std::vector<UnsafeLoopCallCandidate> unsafe_loop_call_candidates;
  std::vector<std::pair<size_t, size_t>> backward_branch_ranges;
  bool saw_backward_branch = false;
  bool saw_branch = false;
  bool saw_global_access = false;
  std::vector<size_t> block_offsets;
  block_offsets.push_back(func.code_offset);
  while (scan_pc < end_pc) {
    const size_t op_pc = scan_pc;
    OpCode op = static_cast<OpCode>(module.code[scan_pc++]);
    switch (op) {
      case OpCode::Nop:
      case OpCode::Trap:
      case OpCode::Breakpoint:
      case OpCode::Leave:
      case OpCode::Safepoint:
      case OpCode::AllocCheckpoint:
      case OpCode::ExitSandbox:
      case OpCode::Yield:
      case OpCode::Fence:
        break;
      case OpCode::CheckCapability:
      case OpCode::EnterSandbox:
      case OpCode::InitGlobal:
      case OpCode::InitModule:
      case OpCode::EnsureModuleInit:
      case OpCode::TraceEnter:
      case OpCode::TraceLeave:
        if (scan_pc + 4 > end_pc) {
          reason = "LLVM JIT metadata operand out of bounds";
          return false;
        }
        ReadU32(module.code, scan_pc);
        break;
      case OpCode::Dup:
      case OpCode::Dup2:
      case OpCode::Swap:
      case OpCode::Rot:
        break;
      case OpCode::Enter:
        if (scan_pc + 2 > end_pc) {
          reason = "LLVM JIT ENTER out of bounds";
          return false;
        }
        local_count = ReadU16(module.code, scan_pc);
        if (local_count < param_count) {
          reason = "LLVM JIT locals < param count";
          return false;
        }
        saw_enter = true;
        break;
      case OpCode::ConstI8:
      case OpCode::ConstU8:
        if (scan_pc + 1 > end_pc) {
          reason = "LLVM JIT 1-byte const out of bounds";
          return false;
        }
        ReadU8(module.code, scan_pc);
        break;
      case OpCode::ConstI16:
      case OpCode::ConstU16:
      case OpCode::ConstChar:
        if (scan_pc + 2 > end_pc) {
          reason = "LLVM JIT 2-byte const out of bounds";
          return false;
        }
        ReadU16(module.code, scan_pc);
        break;
      case OpCode::ConstI32:
        if (scan_pc + 4 > end_pc) {
          reason = "LLVM JIT CONST_I32 out of bounds";
          return false;
        }
        ReadI32(module.code, scan_pc);
        break;
      case OpCode::ConstI64:
        if (scan_pc + 8 > end_pc) {
          reason = "LLVM JIT CONST_I64 out of bounds";
          return false;
        }
        ReadI64(module.code, scan_pc);
        break;
      case OpCode::ConstU32:
        if (scan_pc + 4 > end_pc) {
          reason = "LLVM JIT CONST_U32 out of bounds";
          return false;
        }
        ReadU32(module.code, scan_pc);
        break;
      case OpCode::ConstU64:
        if (scan_pc + 8 > end_pc) {
          reason = "LLVM JIT CONST_U64 out of bounds";
          return false;
        }
        ReadU64(module.code, scan_pc);
        break;
      case OpCode::ConstNull:
      case OpCode::StackTrace:
        break;
      case OpCode::ConstBool:
        if (scan_pc + 1 > end_pc) {
          reason = "LLVM JIT CONST_BOOL out of bounds";
          return false;
        }
        ReadU8(module.code, scan_pc);
        break;
      case OpCode::ConstF32:
        if (scan_pc + 4 > end_pc) {
          reason = "LLVM JIT CONST_F32 out of bounds";
          return false;
        }
        ReadU32(module.code, scan_pc);
        break;
      case OpCode::ConstF64:
        if (scan_pc + 8 > end_pc) {
          reason = "LLVM JIT CONST_F64 out of bounds";
          return false;
        }
        ReadU64(module.code, scan_pc);
        break;
      case OpCode::ConstString: {
        if (scan_pc + 4 > end_pc) {
          reason = "LLVM JIT CONST_STRING operand out of bounds";
          return false;
        }
        uint32_t const_id = ReadU32(module.code, scan_pc);
        if (const_id + 8 > module.const_pool.size()) {
          reason = "LLVM JIT CONST_STRING const out of bounds";
          return false;
        }
        break;
      }
      case OpCode::ConstI128:
      case OpCode::ConstU128: {
        if (scan_pc + 4 > end_pc) {
          reason = "LLVM JIT CONST_I128/U128 operand out of bounds";
          return false;
        }
        uint32_t const_id = ReadU32(module.code, scan_pc);
        if (const_id + 8 > module.const_pool.size()) {
          reason = "LLVM JIT CONST_I128/U128 const out of bounds";
          return false;
        }
        uint32_t kind = read_const_u32(const_id);
        uint32_t want = (op == OpCode::ConstI128) ? 1u : 2u;
        if (kind != want) {
          reason = "LLVM JIT CONST_I128/U128 const kind mismatch";
          return false;
        }
        uint32_t blob_offset = read_const_u32(const_id + 4);
        if (blob_offset + 4 > module.const_pool.size()) {
          reason = "LLVM JIT CONST_I128/U128 blob offset out of bounds";
          return false;
        }
        uint32_t blob_len = read_const_u32(blob_offset);
        if (blob_len < 16 || blob_offset + 4 + blob_len > module.const_pool.size()) {
          reason = "LLVM JIT CONST_I128/U128 blob out of bounds";
          return false;
        }
        break;
      }
      case OpCode::AddI32:
      case OpCode::SubI32:
      case OpCode::MulI32:
      case OpCode::DivI32:
      case OpCode::ModI32:
      case OpCode::NegI32:
      case OpCode::IncI32:
      case OpCode::DecI32:
      case OpCode::AndI32:
      case OpCode::OrI32:
      case OpCode::XorI32:
      case OpCode::ShlI32:
      case OpCode::ShrI32:
      case OpCode::AddU32:
      case OpCode::SubU32:
      case OpCode::MulU32:
      case OpCode::DivU32:
      case OpCode::ModU32:
      case OpCode::IncI8:
      case OpCode::DecI8:
      case OpCode::IncI16:
      case OpCode::DecI16:
      case OpCode::IncU8:
      case OpCode::DecU8:
      case OpCode::IncU16:
      case OpCode::DecU16:
      case OpCode::NegI8:
      case OpCode::NegI16:
      case OpCode::NegU8:
      case OpCode::NegU16:
      case OpCode::IncU32:
      case OpCode::DecU32:
      case OpCode::NegU32:
      case OpCode::AddI64:
      case OpCode::SubI64:
      case OpCode::MulI64:
      case OpCode::DivI64:
      case OpCode::ModI64:
      case OpCode::NegI64:
      case OpCode::IncI64:
      case OpCode::DecI64:
      case OpCode::AndI64:
      case OpCode::OrI64:
      case OpCode::XorI64:
      case OpCode::ShlI64:
      case OpCode::ShrI64:
      case OpCode::AddU64:
      case OpCode::SubU64:
      case OpCode::MulU64:
      case OpCode::DivU64:
      case OpCode::ModU64:
      case OpCode::IncU64:
      case OpCode::DecU64:
      case OpCode::NegU64:
      case OpCode::ConvI32ToI64:
      case OpCode::ConvI64ToI32:
      case OpCode::AddF32:
      case OpCode::SubF32:
      case OpCode::MulF32:
      case OpCode::DivF32:
      case OpCode::NegF32:
      case OpCode::IncF32:
      case OpCode::DecF32:
      case OpCode::AddF64:
      case OpCode::SubF64:
      case OpCode::MulF64:
      case OpCode::DivF64:
      case OpCode::NegF64:
      case OpCode::IncF64:
      case OpCode::DecF64:
      case OpCode::ConvI32ToF32:
      case OpCode::ConvI32ToF64:
      case OpCode::ConvF32ToI32:
      case OpCode::ConvF64ToI32:
      case OpCode::ConvF32ToF64:
      case OpCode::ConvF64ToF32:
      case OpCode::CmpEqI32:
      case OpCode::CmpNeI32:
      case OpCode::CmpLtI32:
      case OpCode::CmpLeI32:
      case OpCode::CmpGtI32:
      case OpCode::CmpGeI32:
      case OpCode::CmpEqU32:
      case OpCode::CmpNeU32:
      case OpCode::CmpLtU32:
      case OpCode::CmpLeU32:
      case OpCode::CmpGtU32:
      case OpCode::CmpGeU32:
      case OpCode::CmpEqI64:
      case OpCode::CmpNeI64:
      case OpCode::CmpLtI64:
      case OpCode::CmpLeI64:
      case OpCode::CmpGtI64:
      case OpCode::CmpGeI64:
      case OpCode::CmpEqU64:
      case OpCode::CmpNeU64:
      case OpCode::CmpLtU64:
      case OpCode::CmpLeU64:
      case OpCode::CmpGtU64:
      case OpCode::CmpGeU64:
      case OpCode::CmpEqF32:
      case OpCode::CmpNeF32:
      case OpCode::CmpLtF32:
      case OpCode::CmpLeF32:
      case OpCode::CmpGtF32:
      case OpCode::CmpGeF32:
      case OpCode::CmpEqF64:
      case OpCode::CmpNeF64:
      case OpCode::CmpLtF64:
      case OpCode::CmpLeF64:
      case OpCode::CmpGtF64:
      case OpCode::CmpGeF64:
      case OpCode::BoolNot:
      case OpCode::BoolAnd:
      case OpCode::BoolOr:
      case OpCode::CheckedBounds:
      case OpCode::IsNull:
      case OpCode::RefEq:
      case OpCode::RefNe:
      case OpCode::Pop:
      case OpCode::KeepAlive:
        break;
      case OpCode::CallCheck:
        break;
      case OpCode::CallNative: {
        if (scan_pc + 5 > end_pc) {
          reason = "LLVM JIT CALL_NATIVE operand out of bounds";
          return false;
        }
        uint32_t ext_id = ReadU32(module.code, scan_pc);
        uint8_t ext_arg = ReadU8(module.code, scan_pc);
        if (!Simple::Byte::IsExtendedOpcodePrefix(static_cast<uint8_t>(op), ext_id, ext_arg)) {
          saw_call = true;
          uint32_t target_func = ext_id;
          uint8_t arg_count = ext_arg;
          if (target_func >= module.functions.size()) { reason = "LLVM JIT CALL_NATIVE invalid function id"; return false; }
          const auto& target_function = module.functions[target_func];
          if (target_function.method_id >= module.methods.size()) { reason = "LLVM JIT CALL_NATIVE invalid method id"; return false; }
          const auto& target_method = module.methods[target_function.method_id];
          if (target_method.sig_id >= module.sigs.size()) { reason = "LLVM JIT CALL_NATIVE invalid signature id"; return false; }
          const auto& target_sig = module.sigs[target_method.sig_id];
          if (arg_count != target_sig.param_count) { reason = "LLVM JIT CALL_NATIVE arg count mismatch"; return false; }
          break;
        }
        if (scan_pc + 2 > end_pc) {
          reason = "LLVM JIT extended opcode out of bounds";
          return false;
        }
        uint16_t ext = ReadU16(module.code, scan_pc);
        switch (static_cast<Simple::Byte::ExtendedOpCode>(ext)) {
          case Simple::Byte::ExtendedOpCode::CheckedAddI32:
          case Simple::Byte::ExtendedOpCode::CheckedSubI32:
          case Simple::Byte::ExtendedOpCode::CheckedMulI32:
          case Simple::Byte::ExtendedOpCode::CheckedDivI32:
          case Simple::Byte::ExtendedOpCode::CheckedModI32:
          case Simple::Byte::ExtendedOpCode::CheckedAddU32:
          case Simple::Byte::ExtendedOpCode::CheckedSubU32:
          case Simple::Byte::ExtendedOpCode::CheckedMulU32:
          case Simple::Byte::ExtendedOpCode::CheckedDivU32:
          case Simple::Byte::ExtendedOpCode::CheckedModU32:
          case Simple::Byte::ExtendedOpCode::CheckedAddI64:
          case Simple::Byte::ExtendedOpCode::CheckedSubI64:
          case Simple::Byte::ExtendedOpCode::CheckedMulI64:
          case Simple::Byte::ExtendedOpCode::CheckedDivI64:
          case Simple::Byte::ExtendedOpCode::CheckedModI64:
          case Simple::Byte::ExtendedOpCode::CheckedAddU64:
          case Simple::Byte::ExtendedOpCode::CheckedSubU64:
          case Simple::Byte::ExtendedOpCode::CheckedMulU64:
          case Simple::Byte::ExtendedOpCode::CheckedDivU64:
          case Simple::Byte::ExtendedOpCode::CheckedModU64:
          case Simple::Byte::ExtendedOpCode::CheckedArrayGetI32:
          case Simple::Byte::ExtendedOpCode::CheckedArraySetI32:
          case Simple::Byte::ExtendedOpCode::CheckedArrayGetI64:
          case Simple::Byte::ExtendedOpCode::CheckedArraySetI64:
          case Simple::Byte::ExtendedOpCode::CheckedArrayGetF32:
          case Simple::Byte::ExtendedOpCode::CheckedArraySetF32:
          case Simple::Byte::ExtendedOpCode::CheckedArrayGetF64:
          case Simple::Byte::ExtendedOpCode::CheckedArraySetF64:
          case Simple::Byte::ExtendedOpCode::CheckedArrayGetRef:
          case Simple::Byte::ExtendedOpCode::CheckedArraySetRef:
          case Simple::Byte::ExtendedOpCode::CheckedListGetI32:
          case Simple::Byte::ExtendedOpCode::CheckedListSetI32:
          case Simple::Byte::ExtendedOpCode::CheckedListGetI64:
          case Simple::Byte::ExtendedOpCode::CheckedListSetI64:
          case Simple::Byte::ExtendedOpCode::CheckedListGetF32:
          case Simple::Byte::ExtendedOpCode::CheckedListSetF32:
          case Simple::Byte::ExtendedOpCode::CheckedListGetF64:
          case Simple::Byte::ExtendedOpCode::CheckedListSetF64:
          case Simple::Byte::ExtendedOpCode::CheckedListGetRef:
          case Simple::Byte::ExtendedOpCode::CheckedListSetRef:
          case Simple::Byte::ExtendedOpCode::CheckedStringGetChar:
          case Simple::Byte::ExtendedOpCode::CheckedStringSlice:
          case Simple::Byte::ExtendedOpCode::InstanceOf:
          case Simple::Byte::ExtendedOpCode::CastRef:
          case Simple::Byte::ExtendedOpCode::CheckedCastRef:
          case Simple::Byte::ExtendedOpCode::LoadVTable:
          case Simple::Byte::ExtendedOpCode::CheckedConvI32ToI64:
          case Simple::Byte::ExtendedOpCode::CheckedConvI64ToI32:
          case Simple::Byte::ExtendedOpCode::CheckedConvI32ToF32:
          case Simple::Byte::ExtendedOpCode::CheckedConvI32ToF64:
          case Simple::Byte::ExtendedOpCode::CheckedConvF32ToI32:
          case Simple::Byte::ExtendedOpCode::CheckedConvF64ToI32:
          case Simple::Byte::ExtendedOpCode::CheckedConvF32ToF64:
          case Simple::Byte::ExtendedOpCode::CheckedConvF64ToF32:
          case Simple::Byte::ExtendedOpCode::GuardBounds:
          case Simple::Byte::ExtendedOpCode::LoadPtr:
          case Simple::Byte::ExtendedOpCode::StorePtr:
          case Simple::Byte::ExtendedOpCode::MemCopy:
          case Simple::Byte::ExtendedOpCode::MemMove:
          case Simple::Byte::ExtendedOpCode::MemSet:
          case Simple::Byte::ExtendedOpCode::MemCompare:
          case Simple::Byte::ExtendedOpCode::Throw:
          case Simple::Byte::ExtendedOpCode::Panic:
          case Simple::Byte::ExtendedOpCode::CaptureLocal:
          case Simple::Byte::ExtendedOpCode::CaptureRef:
          case Simple::Byte::ExtendedOpCode::AddressOfLocal:
          case Simple::Byte::ExtendedOpCode::AddressOfGlobal:
          case Simple::Byte::ExtendedOpCode::Spawn:
          case Simple::Byte::ExtendedOpCode::MakeFuture:
          case Simple::Byte::ExtendedOpCode::EnumTag:
          case Simple::Byte::ExtendedOpCode::VariantTag:
          case Simple::Byte::ExtendedOpCode::EnumPayload:
          case Simple::Byte::ExtendedOpCode::EnumMake:
          case Simple::Byte::ExtendedOpCode::VariantPayload:
          case Simple::Byte::ExtendedOpCode::VariantMake:
          case Simple::Byte::ExtendedOpCode::ResultOk:
          case Simple::Byte::ExtendedOpCode::ResultErr:
          case Simple::Byte::ExtendedOpCode::ResultIsOk:
          case Simple::Byte::ExtendedOpCode::ResultIsErr:
          case Simple::Byte::ExtendedOpCode::ResultUnwrap:
          case Simple::Byte::ExtendedOpCode::ResultPropagateErr:
          case Simple::Byte::ExtendedOpCode::RangeNew:
          case Simple::Byte::ExtendedOpCode::RangeNewStep:
          case Simple::Byte::ExtendedOpCode::RangeNext:
          case Simple::Byte::ExtendedOpCode::IteratorNext:
          case Simple::Byte::ExtendedOpCode::IteratorHasNext:
          case Simple::Byte::ExtendedOpCode::IteratorValue:
          case Simple::Byte::ExtendedOpCode::Join:
          case Simple::Byte::ExtendedOpCode::Await:
          case Simple::Byte::ExtendedOpCode::PollFuture:
          case Simple::Byte::ExtendedOpCode::Detach:
          case Simple::Byte::ExtendedOpCode::Resume:
          case Simple::Byte::ExtendedOpCode::Suspend:
          case Simple::Byte::ExtendedOpCode::ChannelSend:
          case Simple::Byte::ExtendedOpCode::ChannelRecv:
          case Simple::Byte::ExtendedOpCode::ChannelTryRecv:
          case Simple::Byte::ExtendedOpCode::AtomicLoad:
          case Simple::Byte::ExtendedOpCode::AtomicStore:
          case Simple::Byte::ExtendedOpCode::AtomicAdd:
          case Simple::Byte::ExtendedOpCode::AtomicSub:
          case Simple::Byte::ExtendedOpCode::AtomicCompareExchange:
          case Simple::Byte::ExtendedOpCode::Lock:
          case Simple::Byte::ExtendedOpCode::Unlock:
          case Simple::Byte::ExtendedOpCode::TryLock:
          case Simple::Byte::ExtendedOpCode::Wait:
          case Simple::Byte::ExtendedOpCode::Notify:
          case Simple::Byte::ExtendedOpCode::NotifyAll:
          case Simple::Byte::ExtendedOpCode::SourceSpan:
          case Simple::Byte::ExtendedOpCode::Catch:
          case Simple::Byte::ExtendedOpCode::Finally:
          case Simple::Byte::ExtendedOpCode::Deopt:
          case Simple::Byte::ExtendedOpCode::Patchpoint:
          case Simple::Byte::ExtendedOpCode::InlineCache:
          case Simple::Byte::ExtendedOpCode::VecLoad:
          case Simple::Byte::ExtendedOpCode::VecStore:
          case Simple::Byte::ExtendedOpCode::VecSplat:
          case Simple::Byte::ExtendedOpCode::VecExtract:
          case Simple::Byte::ExtendedOpCode::VecAdd:
          case Simple::Byte::ExtendedOpCode::VecSub:
          case Simple::Byte::ExtendedOpCode::VecMul:
          case Simple::Byte::ExtendedOpCode::VecDiv:
          case Simple::Byte::ExtendedOpCode::VecAnd:
          case Simple::Byte::ExtendedOpCode::VecOr:
          case Simple::Byte::ExtendedOpCode::VecXor:
          case Simple::Byte::ExtendedOpCode::PtrAdd:
          case Simple::Byte::ExtendedOpCode::PtrOffset:
          case Simple::Byte::ExtendedOpCode::PtrEq:
          case Simple::Byte::ExtendedOpCode::PtrNe:
          case Simple::Byte::ExtendedOpCode::PtrIsNull:
          case Simple::Byte::ExtendedOpCode::PtrCheckNull:
          case Simple::Byte::ExtendedOpCode::PtrCheckBounds:
            break;
          default:
            reason = std::string("unsupported: extended opcode ") + std::to_string(ext);
            return false;
        }
        break;
      }
      case OpCode::Intrinsic: {
        if (scan_pc + 4 > end_pc) {
          reason = "LLVM JIT INTRINSIC operand out of bounds";
          return false;
        }
        uint32_t id = ReadU32(module.code, scan_pc);
        switch (id) {
          case Simple::VM::kIntrinsicTrap:
          case Simple::VM::kIntrinsicBreakpoint:
          case Simple::VM::kIntrinsicLogI32:
          case Simple::VM::kIntrinsicLogI64:
          case Simple::VM::kIntrinsicLogF32:
          case Simple::VM::kIntrinsicLogF64:
          case Simple::VM::kIntrinsicLogRef:
          case Simple::VM::kIntrinsicPrintAny:
          case Simple::VM::kIntrinsicWriteStdout:
          case Simple::VM::kIntrinsicWriteStderr:
          case Simple::VM::kIntrinsicMonoNs:
          case Simple::VM::kIntrinsicWallNs:
          case Simple::VM::kIntrinsicRandU32:
          case Simple::VM::kIntrinsicRandU64:
          case Simple::VM::kIntrinsicAbsI32:
          case Simple::VM::kIntrinsicAbsI64:
          case Simple::VM::kIntrinsicMinI32:
          case Simple::VM::kIntrinsicMaxI32:
          case Simple::VM::kIntrinsicMinI64:
          case Simple::VM::kIntrinsicMaxI64:
          case Simple::VM::kIntrinsicMinF32:
          case Simple::VM::kIntrinsicMaxF32:
          case Simple::VM::kIntrinsicMinF64:
          case Simple::VM::kIntrinsicMaxF64:
          case Simple::VM::kIntrinsicSqrtF32:
          case Simple::VM::kIntrinsicSqrtF64:
            break;
          default:
            reason = std::string("unsupported: intrinsic ") + std::to_string(id);
            return false;
        }
        break;
      }
      case OpCode::SysCall:
        if (scan_pc + 4 > end_pc) {
          reason = "LLVM JIT SYS_CALL operand out of bounds";
          return false;
        }
        ReadU32(module.code, scan_pc);
        break;
      case OpCode::Line:
        if (scan_pc + 8 > end_pc) {
          reason = "LLVM JIT LINE out of bounds";
          return false;
        }
        ReadU32(module.code, scan_pc);
        ReadU32(module.code, scan_pc);
        break;
      case OpCode::ProfileStart:
      case OpCode::ProfileEnd:
        if (scan_pc + 4 > end_pc) {
          reason = "LLVM JIT PROFILE out of bounds";
          return false;
        }
        ReadU32(module.code, scan_pc);
        break;
      case OpCode::JmpTable: {
        if (scan_pc + 8 > end_pc) {
          reason = "LLVM JIT JMP_TABLE operand out of bounds";
          return false;
        }
        uint32_t const_id = ReadU32(module.code, scan_pc);
        int32_t default_rel = ReadI32(module.code, scan_pc);
        JmpTableInfo table;
        if (!parse_jmp_table(const_id, table, reason)) return false;
        int64_t default_target = static_cast<int64_t>(scan_pc) + default_rel;
        saw_branch = true;
        if (default_target < static_cast<int64_t>(scan_pc)) saw_backward_branch = true;
        if (default_target < static_cast<int64_t>(func.code_offset) || default_target > static_cast<int64_t>(end_pc)) {
          reason = "LLVM JIT JMP_TABLE default target out of bounds";
          return false;
        }
        block_offsets.push_back(static_cast<size_t>(default_target));
        for (int32_t rel : table.rels) {
          int64_t target = static_cast<int64_t>(scan_pc) + rel;
          saw_branch = true;
          if (target < static_cast<int64_t>(scan_pc)) {
            saw_backward_branch = true;
            backward_branch_ranges.push_back({static_cast<size_t>(target), op_pc});
          }
          if (target < static_cast<int64_t>(func.code_offset) || target > static_cast<int64_t>(end_pc)) {
            reason = "LLVM JIT JMP_TABLE target out of bounds";
            return false;
          }
          block_offsets.push_back(static_cast<size_t>(target));
        }
        break;
      }
      case OpCode::Jmp:
      case OpCode::JmpTrue:
      case OpCode::JmpFalse: {
        if (scan_pc + 4 > end_pc) {
          reason = "LLVM JIT jump operand out of bounds";
          return false;
        }
        int32_t rel = ReadI32(module.code, scan_pc);
        saw_branch = true;
        int64_t target = static_cast<int64_t>(scan_pc) + rel;
        if (target < static_cast<int64_t>(scan_pc)) {
          saw_backward_branch = true;
          backward_branch_ranges.push_back({static_cast<size_t>(target), op_pc});
        }
        if (target < static_cast<int64_t>(func.code_offset) || target > static_cast<int64_t>(end_pc)) {
          reason = "LLVM JIT jump target out of bounds";
          return false;
        }
        block_offsets.push_back(static_cast<size_t>(target));
        if (op == OpCode::JmpTrue || op == OpCode::JmpFalse) {
          block_offsets.push_back(scan_pc);
        }
        break;
      }
      case OpCode::CallIndirect: {
        saw_call = true;
        if (scan_pc + 5 > end_pc) {
          reason = "LLVM JIT CALL_INDIRECT operand out of bounds";
          return false;
        }
        uint32_t sig_id = ReadU32(module.code, scan_pc);
        uint8_t arg_count = ReadU8(module.code, scan_pc);
        if (sig_id >= module.sigs.size()) {
          reason = "LLVM JIT CALL_INDIRECT invalid signature id";
          return false;
        }
        const auto& target_sig = module.sigs[sig_id];
        if (arg_count != target_sig.param_count) {
          reason = "LLVM JIT CALL_INDIRECT arg count mismatch";
          return false;
        }
        unsafe_loop_call_candidates.push_back({op_pc,
                                               op,
                                               sig_is_scalar_loop_call_safe(target_sig)
                                                   ? "category=indirect/procedure reason=unknown-target-effects " + signature_label(target_sig)
                                                   : "category=indirect/procedure reason=non-scalar-or-managed-signature " +
                                                         signature_label(target_sig)});
        break;
      }
      case OpCode::CallImport:
      case OpCode::Call:
      case OpCode::TailCall: {
        saw_call = true;
        if (scan_pc + 5 > end_pc) {
          reason = "LLVM JIT CALL operand out of bounds";
          return false;
        }
        uint32_t target_func = ReadU32(module.code, scan_pc);
        uint8_t arg_count = ReadU8(module.code, scan_pc);
        const bool import_like_call =
            target_func < module.function_is_import.size() && module.function_is_import[target_func];
        if (target_func >= module.functions.size()) {
          reason = "LLVM JIT CALL invalid function id";
          return false;
        }
        const auto& target_function = module.functions[target_func];
        if (target_function.method_id >= module.methods.size()) {
          reason = "LLVM JIT CALL invalid method id";
          return false;
        }
        const auto& target_method = module.methods[target_function.method_id];
        if (target_method.sig_id >= module.sigs.size()) {
          reason = "LLVM JIT CALL invalid signature id";
          return false;
        }
        const auto& target_sig = module.sigs[target_method.sig_id];
        if (arg_count != target_sig.param_count) {
          reason = "LLVM JIT CALL arg count mismatch";
          return false;
        }
        std::string unsafe_detail;
        if (import_like_call) {
          unsafe_detail = describe_import_loop_call_safety(target_func, target_sig);
        } else if (!sig_is_scalar_loop_call_safe(target_sig)) {
          unsafe_detail = "category=direct-simple reason=non-scalar-or-managed-signature target=" +
                          call_target_label(target_func, false) + " " + signature_label(target_sig);
        }
        if (!unsafe_detail.empty()) {
          unsafe_loop_call_candidates.push_back({op_pc, op, std::move(unsafe_detail)});
        }
        if (target_func == func_index && arg_count != param_count) {
          reason = "LLVM JIT self CALL arg count mismatch";
          return false;
        }
        break;
      }
      case OpCode::NewClosure: {
        if (scan_pc + 5 > end_pc) {
          reason = "LLVM JIT NEW_CLOSURE operand out of bounds";
          return false;
        }
        uint32_t method_id = ReadU32(module.code, scan_pc);
        uint8_t upvalue_count = ReadU8(module.code, scan_pc);
        if (method_id >= module.methods.size()) {
          reason = "LLVM JIT NEW_CLOSURE bad method id";
          return false;
        }
        if (upvalue_count != 0) {
          reason = "unsupported: NEW_CLOSURE with upvalues needs closure runtime ABI";
          return false;
        }
        break;
      }
      case OpCode::NewObject:
      case OpCode::LoadField:
      case OpCode::StoreField: {
        if (scan_pc + 4 > end_pc) {
          reason = "LLVM JIT object/field operand out of bounds";
          return false;
        }
        uint32_t idx = ReadU32(module.code, scan_pc);
        if (op == OpCode::NewObject && idx >= module.types.size()) {
          reason = "LLVM JIT NEW_OBJECT bad type id";
          return false;
        }
        if ((op == OpCode::LoadField || op == OpCode::StoreField) && idx >= module.fields.size()) {
          reason = "LLVM JIT field id out of range";
          return false;
        }
        break;
      }
      case OpCode::NewArray:
      case OpCode::NewArrayI64:
      case OpCode::NewArrayF32:
      case OpCode::NewArrayF64:
      case OpCode::NewArrayRef:
      case OpCode::NewList:
      case OpCode::NewListI64:
      case OpCode::NewListF32:
      case OpCode::NewListF64:
      case OpCode::NewListRef: {
        if (scan_pc + 8 > end_pc) {
          reason = "LLVM JIT NEW_ARRAY/LIST operand out of bounds";
          return false;
        }
        ReadU32(module.code, scan_pc);
        ReadU32(module.code, scan_pc);
        break;
      }
      case OpCode::LoadLocal:
      case OpCode::StoreLocal: {
        if (scan_pc + 4 > end_pc) {
          reason = "LLVM JIT local operand out of bounds";
          return false;
        }
        uint32_t idx = Simple::VM::Interpreter::ReadU32(module.code, scan_pc);
        if (idx >= local_count) {
          reason = "LLVM JIT local index out of range";
          return false;
        }
        break;
      }
      case OpCode::CheckedNull:
      case OpCode::DropObject:
      case OpCode::CloneObject:
      case OpCode::ObjectEq:
      case OpCode::StringLen:
      case OpCode::StringConcat:
      case OpCode::StringEq:
      case OpCode::StringNe:
      case OpCode::StringCompare:
      case OpCode::StringFind:
      case OpCode::StringGetChar:
      case OpCode::StringSlice:
        break;
      case OpCode::ArrayCopy:
      case OpCode::ArrayFill:
        break;
      case OpCode::ListLen:
        break;
      case OpCode::ListGetI32:
      case OpCode::ListGetI64:
      case OpCode::ListGetF32:
      case OpCode::ListGetF64:
      case OpCode::ListGetRef:
        break;
      case OpCode::ListSetI32:
      case OpCode::ListSetI64:
      case OpCode::ListSetF32:
      case OpCode::ListSetF64:
      case OpCode::ListSetRef:
        break;
      case OpCode::ListPushI32:
      case OpCode::ListPushI64:
      case OpCode::ListPushF32:
      case OpCode::ListPushF64:
      case OpCode::ListPushRef:
        break;
      case OpCode::ListPopI32:
      case OpCode::ListPopI64:
      case OpCode::ListPopF32:
      case OpCode::ListPopF64:
      case OpCode::ListPopRef:
        break;
      case OpCode::ListInsertI32:
      case OpCode::ListInsertI64:
      case OpCode::ListInsertF32:
      case OpCode::ListInsertF64:
      case OpCode::ListInsertRef:
        break;
      case OpCode::ListRemoveI32:
      case OpCode::ListRemoveI64:
      case OpCode::ListRemoveF32:
      case OpCode::ListRemoveF64:
      case OpCode::ListRemoveRef:
      case OpCode::ListClear:
      case OpCode::ListReserve:
      case OpCode::ListResize:
        break;
      case OpCode::TypeOf:
        break;
      case OpCode::ArrayLen:
        break;
      case OpCode::ArrayGetI32:
      case OpCode::ArrayGetI64:
      case OpCode::ArrayGetF32:
      case OpCode::ArrayGetF64:
      case OpCode::ArrayGetRef:
        break;
      case OpCode::ArraySetI32:
      case OpCode::ArraySetI64:
      case OpCode::ArraySetF32:
      case OpCode::ArraySetF64:
      case OpCode::ArraySetRef:
        break;
      case OpCode::LoadGlobal:
      case OpCode::StoreGlobal: {
        saw_global_access = true;
        if (scan_pc + 4 > end_pc) {
          reason = "LLVM JIT global operand out of bounds";
          return false;
        }
        uint32_t idx = Simple::VM::Interpreter::ReadU32(module.code, scan_pc);
        if (idx >= module.globals.size()) {
          reason = "LLVM JIT global index out of range";
          return false;
        }
        break;
      }
      case OpCode::Ret:
      case OpCode::Halt:
        saw_ret = true;
        break;
      default:
        reason = std::string("unsupported opcode: ") + Simple::Byte::OpCodeName(static_cast<uint8_t>(op));
        return false;
    }
  }
  if (!saw_enter || !saw_ret) {
    reason = "unsupported: missing ENTER/RET";
    return false;
  }
  if (saw_call && !options_.allow_runtime_calls) {
    return reject_cached("unsupported: runtime/helper calls need LLVM runtime ABI");
  }
  if (saw_call) {
    if (method.local_count > 64 || func.stack_max > 64) {
      return reject_cached("unsupported: helper call root snapshot exceeds 64 slots");
    }
    if (sig.param_type_start + sig.param_count > module.param_types.size()) {
      reason = "LLVM JIT signature param metadata out of bounds";
      return false;
    }
  }
  if (saw_backward_branch && !unsafe_loop_call_candidates.empty()) {
    const UnsafeLoopCallCandidate* loop_call = nullptr;
    for (const auto& candidate : unsafe_loop_call_candidates) {
      for (const auto& range : backward_branch_ranges) {
        if (candidate.pc >= range.first && candidate.pc <= range.second) {
          loop_call = &candidate;
          break;
        }
      }
      if (loop_call) break;
    }
    if (loop_call) {
      std::string loop_range;
      for (const auto& range : backward_branch_ranges) {
        if (loop_call->pc >= range.first && loop_call->pc <= range.second) {
          loop_range = " loop=[" + std::to_string(range.first - func.code_offset) + "," +
                       std::to_string(range.second - func.code_offset) + "]";
          break;
        }
      }
      return reject_cached("unsupported: import/indirect call inside loop needs LLVM state merge/runtime ABI at pc=" +
                           std::to_string(loop_call->pc - func.code_offset) + loop_range +
                           " op=" + Simple::Byte::OpCodeName(static_cast<uint8_t>(loop_call->op)) +
                           (loop_call->detail.empty() ? std::string() : " " + loop_call->detail));
    }
  }
  (void)saw_call;
  (void)saw_branch;
  if (saw_global_access && !globals_ptr) {
    if (saw_call) {
      reason = "LLVM JIT globals with calls unsupported";
      return false;
    }
    for (const auto& global : module.globals) {
      if (global.init_const_id == 0xFFFFFFFFu) continue;
      if (global.init_const_id + 4 > module.const_pool.size()) {
        reason = "LLVM JIT global init const out of bounds";
        return false;
      }
      uint32_t kind = read_const_u32(global.init_const_id);
      if (kind == 3) {
        if (global.init_const_id + 8 > module.const_pool.size()) {
          reason = "LLVM JIT global init f32 out of bounds";
          return false;
        }
      } else if (kind == 4) {
        if (global.init_const_id + 12 > module.const_pool.size()) {
          reason = "LLVM JIT global init f64 out of bounds";
          return false;
        }
      } else {
        reason = "LLVM JIT initialized global kind unsupported";
        return false;
      }
    }
  }

  auto jit_or_error = llvm::orc::LLJITBuilder().create();
  if (!jit_or_error) {
    reason = ToString(jit_or_error.takeError());
    return false;
  }
  std::unique_ptr<llvm::orc::LLJIT> jit = std::move(*jit_or_error);
  {
    llvm::orc::MangleAndInterner mangle(jit->getExecutionSession(), jit->getDataLayout());
    llvm::orc::SymbolMap symbols;
    symbols[mangle("SimpleVmLlvmCallFunction")] = llvm::orc::ExecutorSymbolDef(
        llvm::orc::ExecutorAddr::fromPtr(&SimpleVmLlvmCallFunction), llvm::JITSymbolFlags::Exported);
    symbols[mangle("SimpleVmLlvmCallDynamicDl")] = llvm::orc::ExecutorSymbolDef(
        llvm::orc::ExecutorAddr::fromPtr(&SimpleVmLlvmCallDynamicDl), llvm::JITSymbolFlags::Exported);
    symbols[mangle("SimpleVmLlvmYield")] = llvm::orc::ExecutorSymbolDef(
        llvm::orc::ExecutorAddr::fromPtr(&SimpleVmLlvmYield), llvm::JITSymbolFlags::Exported);
    symbols[mangle("SimpleVmLlvmTrap")] = llvm::orc::ExecutorSymbolDef(
        llvm::orc::ExecutorAddr::fromPtr(&SimpleVmLlvmTrap), llvm::JITSymbolFlags::Exported);
    symbols[mangle("SimpleVmLlvmPrintAny")] = llvm::orc::ExecutorSymbolDef(
        llvm::orc::ExecutorAddr::fromPtr(&SimpleVmLlvmPrintAny), llvm::JITSymbolFlags::Exported);
    symbols[mangle("SimpleVmLlvmMonoNs")] = llvm::orc::ExecutorSymbolDef(
        llvm::orc::ExecutorAddr::fromPtr(&SimpleVmLlvmMonoNs), llvm::JITSymbolFlags::Exported);
    symbols[mangle("SimpleVmLlvmWallNs")] = llvm::orc::ExecutorSymbolDef(
        llvm::orc::ExecutorAddr::fromPtr(&SimpleVmLlvmWallNs), llvm::JITSymbolFlags::Exported);
    symbols[mangle("SimpleVmLlvmConstString")] = llvm::orc::ExecutorSymbolDef(
        llvm::orc::ExecutorAddr::fromPtr(&SimpleVmLlvmConstString), llvm::JITSymbolFlags::Exported);
    symbols[mangle("SimpleVmLlvmStringLen")] = llvm::orc::ExecutorSymbolDef(
        llvm::orc::ExecutorAddr::fromPtr(&SimpleVmLlvmStringLen), llvm::JITSymbolFlags::Exported);
    symbols[mangle("SimpleVmLlvmStackTrace")] = llvm::orc::ExecutorSymbolDef(
        llvm::orc::ExecutorAddr::fromPtr(&SimpleVmLlvmStackTrace), llvm::JITSymbolFlags::Exported);
    symbols[mangle("SimpleVmLlvmStringConcat")] = llvm::orc::ExecutorSymbolDef(
        llvm::orc::ExecutorAddr::fromPtr(&SimpleVmLlvmStringConcat), llvm::JITSymbolFlags::Exported);
    symbols[mangle("SimpleVmLlvmStringCompare")] = llvm::orc::ExecutorSymbolDef(
        llvm::orc::ExecutorAddr::fromPtr(&SimpleVmLlvmStringCompare), llvm::JITSymbolFlags::Exported);
    symbols[mangle("SimpleVmLlvmStringGetChar")] = llvm::orc::ExecutorSymbolDef(
        llvm::orc::ExecutorAddr::fromPtr(&SimpleVmLlvmStringGetChar), llvm::JITSymbolFlags::Exported);
    symbols[mangle("SimpleVmLlvmStringSlice")] = llvm::orc::ExecutorSymbolDef(
        llvm::orc::ExecutorAddr::fromPtr(&SimpleVmLlvmStringSlice), llvm::JITSymbolFlags::Exported);
    symbols[mangle("SimpleVmLlvmLoadGlobal")] = llvm::orc::ExecutorSymbolDef(
        llvm::orc::ExecutorAddr::fromPtr(&SimpleVmLlvmLoadGlobal), llvm::JITSymbolFlags::Exported);
    symbols[mangle("SimpleVmLlvmStoreGlobal")] = llvm::orc::ExecutorSymbolDef(
        llvm::orc::ExecutorAddr::fromPtr(&SimpleVmLlvmStoreGlobal), llvm::JITSymbolFlags::Exported);
    symbols[mangle("SimpleVmLlvmNewObject")] = llvm::orc::ExecutorSymbolDef(
        llvm::orc::ExecutorAddr::fromPtr(&SimpleVmLlvmNewObject), llvm::JITSymbolFlags::Exported);
    symbols[mangle("SimpleVmLlvmLoadField32")] = llvm::orc::ExecutorSymbolDef(
        llvm::orc::ExecutorAddr::fromPtr(&SimpleVmLlvmLoadField32), llvm::JITSymbolFlags::Exported);
    symbols[mangle("SimpleVmLlvmStoreField32")] = llvm::orc::ExecutorSymbolDef(
        llvm::orc::ExecutorAddr::fromPtr(&SimpleVmLlvmStoreField32), llvm::JITSymbolFlags::Exported);
    symbols[mangle("SimpleVmLlvmTypeOf")] = llvm::orc::ExecutorSymbolDef(
        llvm::orc::ExecutorAddr::fromPtr(&SimpleVmLlvmTypeOf), llvm::JITSymbolFlags::Exported);
    symbols[mangle("SimpleVmLlvmCheckedRef")] = llvm::orc::ExecutorSymbolDef(
        llvm::orc::ExecutorAddr::fromPtr(&SimpleVmLlvmCheckedRef), llvm::JITSymbolFlags::Exported);
    symbols[mangle("SimpleVmLlvmCloneObject")] = llvm::orc::ExecutorSymbolDef(
        llvm::orc::ExecutorAddr::fromPtr(&SimpleVmLlvmCloneObject), llvm::JITSymbolFlags::Exported);
    symbols[mangle("SimpleVmLlvmObjectEq")] = llvm::orc::ExecutorSymbolDef(
        llvm::orc::ExecutorAddr::fromPtr(&SimpleVmLlvmObjectEq), llvm::JITSymbolFlags::Exported);
    symbols[mangle("SimpleVmLlvmInstanceOf")] = llvm::orc::ExecutorSymbolDef(
        llvm::orc::ExecutorAddr::fromPtr(&SimpleVmLlvmInstanceOf), llvm::JITSymbolFlags::Exported);
    symbols[mangle("SimpleVmLlvmCheckedCastRef")] = llvm::orc::ExecutorSymbolDef(
        llvm::orc::ExecutorAddr::fromPtr(&SimpleVmLlvmCheckedCastRef), llvm::JITSymbolFlags::Exported);
    symbols[mangle("SimpleVmLlvmNewArray")] = llvm::orc::ExecutorSymbolDef(
        llvm::orc::ExecutorAddr::fromPtr(&SimpleVmLlvmNewArray), llvm::JITSymbolFlags::Exported);
    symbols[mangle("SimpleVmLlvmArrayLen")] = llvm::orc::ExecutorSymbolDef(
        llvm::orc::ExecutorAddr::fromPtr(&SimpleVmLlvmArrayLen), llvm::JITSymbolFlags::Exported);
    symbols[mangle("SimpleVmLlvmArrayGetI32")] = llvm::orc::ExecutorSymbolDef(
        llvm::orc::ExecutorAddr::fromPtr(&SimpleVmLlvmArrayGetI32), llvm::JITSymbolFlags::Exported);
    symbols[mangle("SimpleVmLlvmArrayGetI64")] = llvm::orc::ExecutorSymbolDef(
        llvm::orc::ExecutorAddr::fromPtr(&SimpleVmLlvmArrayGetI64), llvm::JITSymbolFlags::Exported);
    symbols[mangle("SimpleVmLlvmArrayGetF32")] = llvm::orc::ExecutorSymbolDef(
        llvm::orc::ExecutorAddr::fromPtr(&SimpleVmLlvmArrayGetF32), llvm::JITSymbolFlags::Exported);
    symbols[mangle("SimpleVmLlvmArrayGetF64")] = llvm::orc::ExecutorSymbolDef(
        llvm::orc::ExecutorAddr::fromPtr(&SimpleVmLlvmArrayGetF64), llvm::JITSymbolFlags::Exported);
    symbols[mangle("SimpleVmLlvmArrayGetRef")] = llvm::orc::ExecutorSymbolDef(
        llvm::orc::ExecutorAddr::fromPtr(&SimpleVmLlvmArrayGetRef), llvm::JITSymbolFlags::Exported);
    symbols[mangle("SimpleVmLlvmArraySetI32")] = llvm::orc::ExecutorSymbolDef(
        llvm::orc::ExecutorAddr::fromPtr(&SimpleVmLlvmArraySetI32), llvm::JITSymbolFlags::Exported);
    symbols[mangle("SimpleVmLlvmArraySetI64")] = llvm::orc::ExecutorSymbolDef(
        llvm::orc::ExecutorAddr::fromPtr(&SimpleVmLlvmArraySetI64), llvm::JITSymbolFlags::Exported);
    symbols[mangle("SimpleVmLlvmArraySetF32")] = llvm::orc::ExecutorSymbolDef(
        llvm::orc::ExecutorAddr::fromPtr(&SimpleVmLlvmArraySetF32), llvm::JITSymbolFlags::Exported);
    symbols[mangle("SimpleVmLlvmArraySetF64")] = llvm::orc::ExecutorSymbolDef(
        llvm::orc::ExecutorAddr::fromPtr(&SimpleVmLlvmArraySetF64), llvm::JITSymbolFlags::Exported);
    symbols[mangle("SimpleVmLlvmArraySetRef")] = llvm::orc::ExecutorSymbolDef(
        llvm::orc::ExecutorAddr::fromPtr(&SimpleVmLlvmArraySetRef), llvm::JITSymbolFlags::Exported);
    symbols[mangle("SimpleVmLlvmArrayCopy")] = llvm::orc::ExecutorSymbolDef(
        llvm::orc::ExecutorAddr::fromPtr(&SimpleVmLlvmArrayCopy), llvm::JITSymbolFlags::Exported);
    symbols[mangle("SimpleVmLlvmArrayFill")] = llvm::orc::ExecutorSymbolDef(
        llvm::orc::ExecutorAddr::fromPtr(&SimpleVmLlvmArrayFill), llvm::JITSymbolFlags::Exported);
    symbols[mangle("SimpleVmLlvmNewList")] = llvm::orc::ExecutorSymbolDef(
        llvm::orc::ExecutorAddr::fromPtr(&SimpleVmLlvmNewList), llvm::JITSymbolFlags::Exported);
    symbols[mangle("SimpleVmLlvmListLen")] = llvm::orc::ExecutorSymbolDef(
        llvm::orc::ExecutorAddr::fromPtr(&SimpleVmLlvmListLen), llvm::JITSymbolFlags::Exported);
    symbols[mangle("SimpleVmLlvmListGet32")] = llvm::orc::ExecutorSymbolDef(
        llvm::orc::ExecutorAddr::fromPtr(&SimpleVmLlvmListGet32), llvm::JITSymbolFlags::Exported);
    symbols[mangle("SimpleVmLlvmListGet64")] = llvm::orc::ExecutorSymbolDef(
        llvm::orc::ExecutorAddr::fromPtr(&SimpleVmLlvmListGet64), llvm::JITSymbolFlags::Exported);
    symbols[mangle("SimpleVmLlvmListSet32")] = llvm::orc::ExecutorSymbolDef(
        llvm::orc::ExecutorAddr::fromPtr(&SimpleVmLlvmListSet32), llvm::JITSymbolFlags::Exported);
    symbols[mangle("SimpleVmLlvmListSet64")] = llvm::orc::ExecutorSymbolDef(
        llvm::orc::ExecutorAddr::fromPtr(&SimpleVmLlvmListSet64), llvm::JITSymbolFlags::Exported);
    symbols[mangle("SimpleVmLlvmListPush32")] = llvm::orc::ExecutorSymbolDef(
        llvm::orc::ExecutorAddr::fromPtr(&SimpleVmLlvmListPush32), llvm::JITSymbolFlags::Exported);
    symbols[mangle("SimpleVmLlvmListPush64")] = llvm::orc::ExecutorSymbolDef(
        llvm::orc::ExecutorAddr::fromPtr(&SimpleVmLlvmListPush64), llvm::JITSymbolFlags::Exported);
    symbols[mangle("SimpleVmLlvmListPop32")] = llvm::orc::ExecutorSymbolDef(
        llvm::orc::ExecutorAddr::fromPtr(&SimpleVmLlvmListPop32), llvm::JITSymbolFlags::Exported);
    symbols[mangle("SimpleVmLlvmListPop64")] = llvm::orc::ExecutorSymbolDef(
        llvm::orc::ExecutorAddr::fromPtr(&SimpleVmLlvmListPop64), llvm::JITSymbolFlags::Exported);
    symbols[mangle("SimpleVmLlvmListInsert32")] = llvm::orc::ExecutorSymbolDef(
        llvm::orc::ExecutorAddr::fromPtr(&SimpleVmLlvmListInsert32), llvm::JITSymbolFlags::Exported);
    symbols[mangle("SimpleVmLlvmListInsert64")] = llvm::orc::ExecutorSymbolDef(
        llvm::orc::ExecutorAddr::fromPtr(&SimpleVmLlvmListInsert64), llvm::JITSymbolFlags::Exported);
    symbols[mangle("SimpleVmLlvmListRemove32")] = llvm::orc::ExecutorSymbolDef(
        llvm::orc::ExecutorAddr::fromPtr(&SimpleVmLlvmListRemove32), llvm::JITSymbolFlags::Exported);
    symbols[mangle("SimpleVmLlvmListRemove64")] = llvm::orc::ExecutorSymbolDef(
        llvm::orc::ExecutorAddr::fromPtr(&SimpleVmLlvmListRemove64), llvm::JITSymbolFlags::Exported);
    symbols[mangle("SimpleVmLlvmListClear")] = llvm::orc::ExecutorSymbolDef(
        llvm::orc::ExecutorAddr::fromPtr(&SimpleVmLlvmListClear), llvm::JITSymbolFlags::Exported);
    symbols[mangle("SimpleVmLlvmListReserve")] = llvm::orc::ExecutorSymbolDef(
        llvm::orc::ExecutorAddr::fromPtr(&SimpleVmLlvmListReserve), llvm::JITSymbolFlags::Exported);
    symbols[mangle("SimpleVmLlvmListResize")] = llvm::orc::ExecutorSymbolDef(
        llvm::orc::ExecutorAddr::fromPtr(&SimpleVmLlvmListResize), llvm::JITSymbolFlags::Exported);
    if (llvm::Error err = jit->getMainJITDylib().define(llvm::orc::absoluteSymbols(std::move(symbols)))) {
      reason = ToString(std::move(err));
      return false;
    }
  }

  auto context = std::make_unique<llvm::LLVMContext>();
  auto ir_module = std::make_unique<llvm::Module>("simple_llvm_jit_module", *context);
  ir_module->setDataLayout(jit->getDataLayout());

  llvm::IRBuilder<> builder(*context);
  llvm::Type* i64 = builder.getInt64Ty();
  llvm::Type* i32 = builder.getInt32Ty();
  llvm::Type* f32 = builder.getFloatTy();
  llvm::Type* f64 = builder.getDoubleTy();
  llvm::PointerType* slot_ptr = llvm::PointerType::getUnqual(*context);
  llvm::FunctionType* fn_type = llvm::FunctionType::get(i64, {slot_ptr, i32}, false);
  llvm::FunctionType* helper_type = llvm::FunctionType::get(
      i64, {slot_ptr, i32, slot_ptr, i32, slot_ptr, i32, i64, slot_ptr, i32, i64, i32, i32,
            builder.getInt8Ty(), builder.getInt8Ty(), slot_ptr}, false);
  llvm::FunctionType* yield_type = llvm::FunctionType::get(builder.getVoidTy(), {}, false);
  llvm::FunctionType* trap_type = llvm::FunctionType::get(builder.getVoidTy(), {}, false);
  llvm::FunctionType* string_compare_type = llvm::FunctionType::get(i64, {i64, i64, i32}, false);
  llvm::FunctionType* string_slice_type = llvm::FunctionType::get(i64, {i64, i64, i64}, false);
  llvm::FunctionType* load_global_type = llvm::FunctionType::get(i64, {i32}, false);
  llvm::FunctionType* store_global_type = llvm::FunctionType::get(builder.getVoidTy(), {i32, i64}, false);
  llvm::FunctionType* new_object_type = llvm::FunctionType::get(i64, {i32, i32}, false);
  llvm::FunctionType* load_field_type = llvm::FunctionType::get(i64, {i64, i32}, false);
  llvm::FunctionType* object_eq_type = llvm::FunctionType::get(i64, {i64, i64}, false);
  llvm::FunctionType* store_field_type = llvm::FunctionType::get(builder.getVoidTy(), {i64, i32, i64}, false);
  llvm::FunctionType* new_array_type = llvm::FunctionType::get(i64, {i32, i32, i32}, false);
  llvm::FunctionType* array_len_type = llvm::FunctionType::get(i64, {i64}, false);
  llvm::FunctionType* array_get_type = llvm::FunctionType::get(i64, {i64, i64}, false);
  llvm::FunctionType* list_push_type = llvm::FunctionType::get(builder.getVoidTy(), {i64, i64}, false);
  llvm::FunctionType* list_clear_type = llvm::FunctionType::get(builder.getVoidTy(), {i64}, false);
  llvm::FunctionType* array_set_type = llvm::FunctionType::get(builder.getVoidTy(), {i64, i64, i64}, false);
  llvm::FunctionType* array_copy_type = llvm::FunctionType::get(builder.getVoidTy(), {i64, i64, i64, i64, i64}, false);
  llvm::FunctionCallee call_helper = ir_module->getOrInsertFunction("SimpleVmLlvmCallFunction", helper_type);
  llvm::FunctionType* dynamic_dl_type = llvm::FunctionType::get(
      i64,
      {slot_ptr, i32, slot_ptr, i32, slot_ptr, i32, i64, slot_ptr, i32, i64, i32, i32, slot_ptr},
      false);
  llvm::FunctionCallee dynamic_dl_helper = ir_module->getOrInsertFunction("SimpleVmLlvmCallDynamicDl", dynamic_dl_type);
  llvm::FunctionCallee yield_helper = ir_module->getOrInsertFunction("SimpleVmLlvmYield", yield_type);
  llvm::FunctionCallee trap_helper = ir_module->getOrInsertFunction("SimpleVmLlvmTrap", trap_type);
  llvm::FunctionCallee print_any_helper = ir_module->getOrInsertFunction("SimpleVmLlvmPrintAny", llvm::FunctionType::get(builder.getVoidTy(), {i64, i32}, false));
  llvm::FunctionCallee mono_ns_helper = ir_module->getOrInsertFunction("SimpleVmLlvmMonoNs", llvm::FunctionType::get(i64, {}, false));
  llvm::FunctionCallee wall_ns_helper = ir_module->getOrInsertFunction("SimpleVmLlvmWallNs", llvm::FunctionType::get(i64, {}, false));
  llvm::FunctionCallee const_string_helper = ir_module->getOrInsertFunction("SimpleVmLlvmConstString", load_global_type);
  llvm::FunctionCallee string_len_helper = ir_module->getOrInsertFunction("SimpleVmLlvmStringLen", array_len_type);
  llvm::FunctionCallee stack_trace_helper = ir_module->getOrInsertFunction("SimpleVmLlvmStackTrace", llvm::FunctionType::get(i64, {}, false));
  llvm::FunctionCallee string_concat_helper = ir_module->getOrInsertFunction("SimpleVmLlvmStringConcat", object_eq_type);
  llvm::FunctionCallee string_compare_helper = ir_module->getOrInsertFunction("SimpleVmLlvmStringCompare", string_compare_type);
  llvm::FunctionCallee string_get_char_helper = ir_module->getOrInsertFunction("SimpleVmLlvmStringGetChar", array_get_type);
  llvm::FunctionCallee string_slice_helper = ir_module->getOrInsertFunction("SimpleVmLlvmStringSlice", string_slice_type);
  llvm::FunctionCallee load_global_helper = ir_module->getOrInsertFunction("SimpleVmLlvmLoadGlobal", load_global_type);
  llvm::FunctionCallee store_global_helper = ir_module->getOrInsertFunction("SimpleVmLlvmStoreGlobal", store_global_type);
  llvm::FunctionCallee new_object_helper = ir_module->getOrInsertFunction("SimpleVmLlvmNewObject", new_object_type);
  llvm::FunctionCallee load_field_helper = ir_module->getOrInsertFunction("SimpleVmLlvmLoadField32", load_field_type);
  llvm::FunctionCallee store_field_helper = ir_module->getOrInsertFunction("SimpleVmLlvmStoreField32", store_field_type);
  llvm::FunctionCallee type_of_helper = ir_module->getOrInsertFunction("SimpleVmLlvmTypeOf", array_len_type);
  llvm::FunctionCallee checked_ref_helper = ir_module->getOrInsertFunction("SimpleVmLlvmCheckedRef", array_len_type);
  llvm::FunctionCallee clone_object_helper = ir_module->getOrInsertFunction("SimpleVmLlvmCloneObject", array_len_type);
  llvm::FunctionCallee object_eq_helper = ir_module->getOrInsertFunction("SimpleVmLlvmObjectEq", object_eq_type);
  llvm::FunctionCallee instance_of_helper = ir_module->getOrInsertFunction("SimpleVmLlvmInstanceOf", object_eq_type);
  llvm::FunctionCallee checked_cast_ref_helper = ir_module->getOrInsertFunction("SimpleVmLlvmCheckedCastRef", object_eq_type);
  llvm::FunctionCallee new_array_helper = ir_module->getOrInsertFunction("SimpleVmLlvmNewArray", new_array_type);
  llvm::FunctionCallee array_len_helper = ir_module->getOrInsertFunction("SimpleVmLlvmArrayLen", array_len_type);
  llvm::FunctionCallee array_get_i32_helper = ir_module->getOrInsertFunction("SimpleVmLlvmArrayGetI32", array_get_type);
  llvm::FunctionCallee array_get_i64_helper = ir_module->getOrInsertFunction("SimpleVmLlvmArrayGetI64", array_get_type);
  llvm::FunctionCallee array_get_f32_helper = ir_module->getOrInsertFunction("SimpleVmLlvmArrayGetF32", array_get_type);
  llvm::FunctionCallee array_get_f64_helper = ir_module->getOrInsertFunction("SimpleVmLlvmArrayGetF64", array_get_type);
  llvm::FunctionCallee array_get_ref_helper = ir_module->getOrInsertFunction("SimpleVmLlvmArrayGetRef", array_get_type);
  llvm::FunctionCallee array_set_i32_helper = ir_module->getOrInsertFunction("SimpleVmLlvmArraySetI32", array_set_type);
  llvm::FunctionCallee array_set_i64_helper = ir_module->getOrInsertFunction("SimpleVmLlvmArraySetI64", array_set_type);
  llvm::FunctionCallee array_set_f32_helper = ir_module->getOrInsertFunction("SimpleVmLlvmArraySetF32", array_set_type);
  llvm::FunctionCallee array_set_f64_helper = ir_module->getOrInsertFunction("SimpleVmLlvmArraySetF64", array_set_type);
  llvm::FunctionCallee array_set_ref_helper = ir_module->getOrInsertFunction("SimpleVmLlvmArraySetRef", array_set_type);
  llvm::FunctionCallee array_copy_helper = ir_module->getOrInsertFunction("SimpleVmLlvmArrayCopy", array_copy_type);
  llvm::FunctionCallee array_fill_helper = ir_module->getOrInsertFunction("SimpleVmLlvmArrayFill", array_set_type);
  llvm::FunctionCallee new_list_helper = ir_module->getOrInsertFunction("SimpleVmLlvmNewList", new_array_type);
  llvm::FunctionCallee list_len_helper = ir_module->getOrInsertFunction("SimpleVmLlvmListLen", array_len_type);
  llvm::FunctionCallee list_get32_helper = ir_module->getOrInsertFunction("SimpleVmLlvmListGet32", array_get_type);
  llvm::FunctionCallee list_get64_helper = ir_module->getOrInsertFunction("SimpleVmLlvmListGet64", array_get_type);
  llvm::FunctionCallee list_set32_helper = ir_module->getOrInsertFunction("SimpleVmLlvmListSet32", array_set_type);
  llvm::FunctionCallee list_set64_helper = ir_module->getOrInsertFunction("SimpleVmLlvmListSet64", array_set_type);
  llvm::FunctionCallee list_push32_helper = ir_module->getOrInsertFunction("SimpleVmLlvmListPush32", list_push_type);
  llvm::FunctionCallee list_push64_helper = ir_module->getOrInsertFunction("SimpleVmLlvmListPush64", list_push_type);
  llvm::FunctionCallee list_pop32_helper = ir_module->getOrInsertFunction("SimpleVmLlvmListPop32", array_len_type);
  llvm::FunctionCallee list_pop64_helper = ir_module->getOrInsertFunction("SimpleVmLlvmListPop64", array_len_type);
  llvm::FunctionCallee list_insert32_helper = ir_module->getOrInsertFunction("SimpleVmLlvmListInsert32", array_set_type);
  llvm::FunctionCallee list_insert64_helper = ir_module->getOrInsertFunction("SimpleVmLlvmListInsert64", array_set_type);
  llvm::FunctionCallee list_remove32_helper = ir_module->getOrInsertFunction("SimpleVmLlvmListRemove32", array_get_type);
  llvm::FunctionCallee list_remove64_helper = ir_module->getOrInsertFunction("SimpleVmLlvmListRemove64", array_get_type);
  llvm::FunctionCallee list_clear_helper = ir_module->getOrInsertFunction("SimpleVmLlvmListClear", list_clear_type);
  llvm::FunctionCallee list_reserve_helper = ir_module->getOrInsertFunction("SimpleVmLlvmListReserve", list_push_type);
  llvm::FunctionCallee list_resize_helper = ir_module->getOrInsertFunction("SimpleVmLlvmListResize", array_set_type);
  const std::string symbol_name = "simple_jit_func_" + std::to_string(func_index);
  llvm::Function* fn = llvm::Function::Create(fn_type, llvm::Function::ExternalLinkage, symbol_name, ir_module.get());
  llvm::BasicBlock* entry = llvm::BasicBlock::Create(*context, "entry", fn);
  llvm::BasicBlock* body_entry = llvm::BasicBlock::Create(*context, "bc" + std::to_string(func.code_offset), fn);
  builder.SetInsertPoint(entry);
  llvm::Value* module_ptr = builder.CreateIntToPtr(
      builder.getInt64(reinterpret_cast<uintptr_t>(&module)), slot_ptr);

  std::vector<llvm::Value*> stack;
  std::vector<llvm::AllocaInst*> locals;
  std::vector<llvm::AllocaInst*> globals;
  bool native_has_ret = true;
  std::unordered_map<size_t, llvm::BasicBlock*> blocks;
  std::unordered_map<size_t, std::vector<llvm::Value*>> block_stacks;
  std::unordered_map<size_t, std::vector<uint32_t>> block_stack_types;
  std::unordered_map<size_t, std::vector<uint32_t>> block_local_types;
  std::unordered_map<size_t, std::vector<std::pair<llvm::BasicBlock*, std::vector<llvm::Value*>>>> block_stack_incomings;
  std::unordered_map<llvm::Value*, uint32_t> value_type_ids;
  std::vector<uint32_t> local_type_ids;
  constexpr uint32_t kUnknownJitTypeId = 0xFFFFFFFFu;
  blocks.emplace(func.code_offset, body_entry);
  auto get_block = [&](size_t offset) -> llvm::BasicBlock* {
    auto it = blocks.find(offset);
    if (it != blocks.end()) return it->second;
    llvm::BasicBlock* block = llvm::BasicBlock::Create(*context, "bc" + std::to_string(offset), fn);
    blocks.emplace(offset, block);
    return block;
  };
  for (size_t offset : block_offsets) {
    if (offset < end_pc) (void)get_block(offset);
  }
  size_t pc = func.code_offset;
  auto to_i32 = [&](llvm::Value* value) -> llvm::Value* {
    if (value->getType()->isIntegerTy(32)) return value;
    return builder.CreateTrunc(value, i32);
  };
  auto to_i8 = [&](llvm::Value* value) -> llvm::Value* {
    return builder.CreateTrunc(to_i32(value), builder.getInt8Ty());
  };
  auto to_i16 = [&](llvm::Value* value) -> llvm::Value* {
    return builder.CreateTrunc(to_i32(value), builder.getInt16Ty());
  };
  auto sext_i8_to_i32 = [&](llvm::Value* value) -> llvm::Value* {
    return builder.CreateSExt(value, i32);
  };
  auto sext_i16_to_i32 = [&](llvm::Value* value) -> llvm::Value* {
    return builder.CreateSExt(value, i32);
  };
  auto zext_i8_to_i32 = [&](llvm::Value* value) -> llvm::Value* {
    return builder.CreateZExt(value, i32);
  };
  auto zext_i16_to_i32 = [&](llvm::Value* value) -> llvm::Value* {
    return builder.CreateZExt(value, i32);
  };
  auto to_slot = [&](llvm::Value* value) -> llvm::Value* {
    if (value->getType()->isIntegerTy(64)) return value;
    return builder.CreateZExt(value, i64);
  };
  auto to_f32 = [&](llvm::Value* value) -> llvm::Value* {
    llvm::Value* bits = to_i32(value);
    return builder.CreateBitCast(bits, f32);
  };
  auto to_f64 = [&](llvm::Value* value) -> llvm::Value* {
    llvm::Value* bits = to_slot(value);
    return builder.CreateBitCast(bits, f64);
  };
  auto f32_to_slot_bits = [&](llvm::Value* value) -> llvm::Value* {
    return builder.CreateBitCast(value, i32);
  };
  auto f64_to_slot_bits = [&](llvm::Value* value) -> llvm::Value* {
    return builder.CreateBitCast(value, i64);
  };
  auto create_entry_alloca = [&](llvm::Type* type, llvm::Value* array_size, const llvm::Twine& name) -> llvm::AllocaInst* {
    llvm::IRBuilder<> alloca_builder(entry);
    if (llvm::Instruction* term = entry->getTerminator()) alloca_builder.SetInsertPoint(term);
    return alloca_builder.CreateAlloca(type, array_size, name);
  };
  auto value_type_id = [&](llvm::Value* value) -> uint32_t {
    auto it = value_type_ids.find(value);
    return it == value_type_ids.end() ? kUnknownJitTypeId : it->second;
  };
  auto note_value_type = [&](llvm::Value* value, uint32_t type_id) {
    if (value && type_id != kUnknownJitTypeId) value_type_ids[value] = type_id;
  };
  auto root_mask_for_type_ids = [&](const std::vector<uint32_t>& type_ids) -> uint64_t {
    uint64_t mask = 0;
    const size_t limit = std::min<size_t>(type_ids.size(), 64);
    for (size_t i = 0; i < limit; ++i) {
      if (type_ids[i] != kUnknownJitTypeId && Simple::VM::Jit::IsJitRootType(module, type_ids[i])) {
        mask |= uint64_t{1} << i;
      }
    }
    return mask;
  };
  auto stack_ref_mask = [&]() -> uint64_t {
    std::vector<uint32_t> stack_type_ids;
    stack_type_ids.reserve(stack.size());
    for (llvm::Value* value : stack) stack_type_ids.push_back(value_type_id(value));
    return root_mask_for_type_ids(stack_type_ids);
  };
  auto local_ref_mask = [&]() -> uint64_t {
    return root_mask_for_type_ids(local_type_ids);
  };
  auto merge_local_types = [&](size_t target) -> bool {
    auto existing_it = block_local_types.find(target);
    if (existing_it == block_local_types.end()) {
      block_local_types[target] = local_type_ids;
      return true;
    }
    std::vector<uint32_t>& existing = existing_it->second;
    if (existing.size() != local_type_ids.size()) {
      reason = "LLVM JIT branch local root map mismatch";
      return false;
    }
    for (size_t i = 0; i < existing.size(); ++i) {
      if (existing[i] != local_type_ids[i]) existing[i] = kUnknownJitTypeId;
    }
    return true;
  };
  auto merge_block_stack = [&](size_t target, const std::vector<llvm::Value*>& incoming_stack,
                               llvm::BasicBlock* pred) -> bool {
    std::vector<llvm::Value*> normalized;
    std::vector<uint32_t> normalized_types;
    normalized.reserve(incoming_stack.size());
    normalized_types.reserve(incoming_stack.size());
    for (llvm::Value* value : incoming_stack) {
      const uint32_t type_id = value_type_id(value);
      llvm::Value* slot_value = to_slot(value);
      note_value_type(slot_value, type_id);
      normalized.push_back(slot_value);
      normalized_types.push_back(type_id);
    }
    if (!merge_local_types(target)) return false;
    auto existing_it = block_stacks.find(target);
    if (existing_it == block_stacks.end()) {
      block_stacks[target] = normalized;
      block_stack_types[target] = normalized_types;
      block_stack_incomings[target].push_back({pred, normalized});
      return true;
    }
    std::vector<llvm::Value*>& existing = existing_it->second;
    std::vector<uint32_t>& existing_types = block_stack_types[target];
    if (existing.size() != normalized.size() || existing_types.size() != normalized_types.size()) {
      reason = "LLVM JIT branch stack height mismatch";
      return false;
    }
    llvm::BasicBlock* target_block = get_block(target);
    auto& incomings = block_stack_incomings[target];
    for (size_t i = 0; i < existing.size(); ++i) {
      if (existing_types[i] != normalized_types[i]) existing_types[i] = kUnknownJitTypeId;
      llvm::PHINode* phi = llvm::dyn_cast<llvm::PHINode>(existing[i]);
      if (!phi || phi->getParent() != target_block) {
        llvm::IRBuilder<> phi_builder(target_block, target_block->begin());
        phi = phi_builder.CreatePHI(i64, static_cast<unsigned>(incomings.size() + 1), "stack.merge");
        for (const auto& previous : incomings) {
          phi->addIncoming(previous.second[i], previous.first);
        }
        existing[i] = phi;
      }
      note_value_type(phi, existing_types[i]);
      phi->addIncoming(normalized[i], pred);
    }
    incomings.push_back({pred, normalized});
    return true;
  };
  auto emit_call_helper = [&](llvm::Value* target_func,
                              llvm::AllocaInst* call_args,
                              uint8_t arg_count,
                              llvm::AllocaInst* has_ret_ptr,
                              size_t call_pc,
                              bool may_block,
                              bool may_allocate) -> llvm::Value* {
    llvm::Value* null_slots = llvm::ConstantPointerNull::get(slot_ptr);
    llvm::Value* local_snapshot = null_slots;
    if (!locals.empty()) {
      llvm::AllocaInst* snapshot = create_entry_alloca(i64, builder.getInt32(static_cast<uint32_t>(locals.size())), "caller_locals");
      for (size_t i = 0; i < locals.size(); ++i) {
        llvm::Value* ptr = builder.CreateGEP(i64, snapshot, builder.getInt64(static_cast<uint64_t>(i)));
        builder.CreateStore(builder.CreateLoad(i64, locals[i]), ptr);
      }
      local_snapshot = snapshot;
    }
    llvm::Value* stack_snapshot = null_slots;
    if (!stack.empty()) {
      llvm::AllocaInst* snapshot = create_entry_alloca(i64, builder.getInt32(static_cast<uint32_t>(stack.size())), "caller_stack");
      for (size_t i = 0; i < stack.size(); ++i) {
        llvm::Value* ptr = builder.CreateGEP(i64, snapshot, builder.getInt64(static_cast<uint64_t>(i)));
        builder.CreateStore(to_slot(stack[i]), ptr);
      }
      stack_snapshot = snapshot;
    }
    return builder.CreateCall(call_helper,
                              {module_ptr, target_func, call_args, builder.getInt32(arg_count),
                               local_snapshot, builder.getInt32(static_cast<uint32_t>(locals.size())),
                               builder.getInt64(local_ref_mask()), stack_snapshot,
                               builder.getInt32(static_cast<uint32_t>(stack.size())), builder.getInt64(stack_ref_mask()),
                               builder.getInt32(static_cast<uint32_t>(func_index)),
                               builder.getInt32(static_cast<uint32_t>(call_pc - func.code_offset)),
                               builder.getInt8(may_block ? 1 : 0), builder.getInt8(may_allocate ? 1 : 0), has_ret_ptr});
  };
  auto emit_dynamic_dl_helper = [&](uint32_t target_func,
                                    llvm::AllocaInst* call_args,
                                    uint8_t arg_count,
                                    llvm::AllocaInst* has_ret_ptr,
                                    size_t call_pc) -> llvm::Value* {
    llvm::Value* null_slots = llvm::ConstantPointerNull::get(slot_ptr);
    llvm::Value* local_snapshot = null_slots;
    if (!locals.empty()) {
      llvm::AllocaInst* snapshot = create_entry_alloca(i64, builder.getInt32(static_cast<uint32_t>(locals.size())),
                                                       "dynamic_dl_caller_locals");
      for (size_t i = 0; i < locals.size(); ++i) {
        llvm::Value* ptr = builder.CreateGEP(i64, snapshot, builder.getInt64(static_cast<uint64_t>(i)));
        builder.CreateStore(builder.CreateLoad(i64, locals[i]), ptr);
      }
      local_snapshot = snapshot;
    }
    llvm::Value* stack_snapshot = null_slots;
    if (!stack.empty()) {
      llvm::AllocaInst* snapshot = create_entry_alloca(i64, builder.getInt32(static_cast<uint32_t>(stack.size())),
                                                       "dynamic_dl_caller_stack");
      for (size_t i = 0; i < stack.size(); ++i) {
        llvm::Value* ptr = builder.CreateGEP(i64, snapshot, builder.getInt64(static_cast<uint64_t>(i)));
        builder.CreateStore(to_slot(stack[i]), ptr);
      }
      stack_snapshot = snapshot;
    }
    return builder.CreateCall(dynamic_dl_helper,
                              {module_ptr, builder.getInt32(target_func), call_args, builder.getInt32(arg_count),
                               local_snapshot, builder.getInt32(static_cast<uint32_t>(locals.size())),
                               builder.getInt64(local_ref_mask()), stack_snapshot,
                               builder.getInt32(static_cast<uint32_t>(stack.size())), builder.getInt64(stack_ref_mask()),
                               builder.getInt32(static_cast<uint32_t>(func_index)),
                               builder.getInt32(static_cast<uint32_t>(call_pc - func.code_offset)), has_ret_ptr});
  };
  auto emit_trap_if = [&](llvm::Value* cond, const char* name) {
    llvm::BasicBlock* trap_block = llvm::BasicBlock::Create(*context, std::string(name) + ".trap", fn);
    llvm::BasicBlock* cont_block = llvm::BasicBlock::Create(*context, std::string(name) + ".cont", fn);
    builder.CreateCondBr(cond, trap_block, cont_block);
    builder.SetInsertPoint(trap_block);
    builder.CreateCall(trap_helper, {});
    builder.CreateRet(builder.getInt64(0));
    builder.SetInsertPoint(cont_block);
  };
  auto emit_call_like = [&](uint32_t target_func, uint8_t arg_count, bool tail, const char* opname) -> bool {
    if (target_func >= module.functions.size()) { reason = std::string("LLVM JIT ") + opname + " invalid function id"; return false; }
    const auto& target_function = module.functions[target_func];
    if (target_function.method_id >= module.methods.size()) { reason = std::string("LLVM JIT ") + opname + " invalid method id"; return false; }
    const auto& target_method = module.methods[target_function.method_id];
    if (target_method.sig_id >= module.sigs.size()) { reason = std::string("LLVM JIT ") + opname + " invalid signature id"; return false; }
    const auto& target_sig = module.sigs[target_method.sig_id];
    if (arg_count != target_sig.param_count) { reason = std::string("LLVM JIT ") + opname + " arg count mismatch"; return false; }
    if (target_func == func_index && arg_count != param_count) { reason = std::string("LLVM JIT ") + opname + " self arg count mismatch"; return false; }
    if (stack.size() < arg_count) { reason = std::string("LLVM JIT ") + opname + " stack underflow"; return false; }
    llvm::AllocaInst* call_args = create_entry_alloca(i64, builder.getInt32(arg_count), std::string(opname) + "_args");
    for (int i = static_cast<int>(arg_count) - 1; i >= 0; --i) {
      llvm::Value* arg = to_slot(stack.back());
      stack.pop_back();
      llvm::Value* ptr = builder.CreateGEP(i64, call_args, builder.getInt64(static_cast<uint64_t>(i)));
      builder.CreateStore(arg, ptr);
    }
    llvm::Value* result = nullptr;
    if (target_func == func_index) {
      result = builder.CreateCall(fn_type, fn, {call_args, builder.getInt32(arg_count)});
    } else {
      llvm::AllocaInst* has_ret_ptr = create_entry_alloca(builder.getInt8Ty(), nullptr, std::string(opname) + "_has_ret");
      builder.CreateStore(builder.getInt8(0), has_ret_ptr);
      const auto [may_block, may_allocate] = helper_call_safepoint_flags(target_func);
      result = emit_call_helper(builder.getInt32(target_func), call_args, arg_count, has_ret_ptr, pc,
                                may_block, may_allocate);
    }
    const bool returns_void = sig_returns_void(target_sig);
    if (tail) {
      native_has_ret = !returns_void;
      builder.CreateRet(returns_void ? builder.getInt64(0) : result);
      stack.clear();
    } else if (!returns_void) {
      note_value_type(result, target_sig.ret_type_id);
      stack.push_back(result);
    }
    return true;
  };

  while (pc < end_pc) {
    auto block_it = blocks.find(pc);
    if (block_it != blocks.end()) {
      if (builder.GetInsertBlock() != block_it->second && !builder.GetInsertBlock()->getTerminator()) {
        builder.CreateBr(block_it->second);
      }
      builder.SetInsertPoint(block_it->second);
      auto stack_it = block_stacks.find(pc);
      if (stack_it != block_stacks.end()) stack = stack_it->second;
      auto local_type_it = block_local_types.find(pc);
      if (local_type_it != block_local_types.end()) local_type_ids = local_type_it->second;
    }
    const bool skipping_unreachable = builder.GetInsertBlock()->getTerminator() != nullptr;
    const size_t instr_pc = pc;
    OpCode op = static_cast<OpCode>(module.code[pc++]);
    switch (op) {
      case OpCode::Nop:
      case OpCode::Breakpoint:
      case OpCode::Leave:
      case OpCode::Safepoint:
      case OpCode::AllocCheckpoint:
      case OpCode::ExitSandbox:
      case OpCode::CallCheck:
        break;
      case OpCode::ArrayCopy: {
        if (skipping_unreachable) break;
        if (stack.size() < 5) { reason = "LLVM JIT ARRAY_COPY underflow"; return false; }
        llvm::Value* count = to_slot(stack.back()); stack.pop_back();
        llvm::Value* dst_index = to_slot(stack.back()); stack.pop_back();
        llvm::Value* dst = to_slot(stack.back()); stack.pop_back();
        llvm::Value* src_index = to_slot(stack.back()); stack.pop_back();
        llvm::Value* src = to_slot(stack.back()); stack.pop_back();
        builder.CreateCall(array_copy_helper, {src, src_index, dst, dst_index, count});
        break;
      }
      case OpCode::ArrayFill: {
        if (skipping_unreachable) break;
        if (stack.size() < 3) { reason = "LLVM JIT ARRAY_FILL underflow"; return false; }
        llvm::Value* fill = to_slot(stack.back()); stack.pop_back();
        llvm::Value* count = to_slot(stack.back()); stack.pop_back();
        llvm::Value* ref = to_slot(stack.back()); stack.pop_back();
        builder.CreateCall(array_fill_helper, {ref, count, fill});
        break;
      }
      case OpCode::Trap:
        if (skipping_unreachable) break;
        builder.CreateCall(trap_helper, {});
        builder.CreateRet(builder.getInt64(0));
        stack.clear();
        break;
      case OpCode::Yield:
        if (skipping_unreachable) break;
        builder.CreateCall(yield_helper, {});
        break;
      case OpCode::Fence:
        if (skipping_unreachable) break;
        builder.CreateFence(llvm::AtomicOrdering::SequentiallyConsistent);
        break;
      case OpCode::CallNative: {
        uint32_t ext_id = ReadU32(module.code, pc);
        uint8_t ext_arg = ReadU8(module.code, pc);
        uint16_t ext = 0;
        if (Simple::Byte::IsExtendedOpcodePrefix(static_cast<uint8_t>(op), ext_id, ext_arg)) {
          ext = ReadU16(module.code, pc);
        }
        if (skipping_unreachable) break;
        if (!Simple::Byte::IsExtendedOpcodePrefix(static_cast<uint8_t>(op), ext_id, ext_arg)) {
          if (!emit_call_like(ext_id, ext_arg, false, "CALL_NATIVE")) return false;
          break;
        }
        switch (static_cast<Simple::Byte::ExtendedOpCode>(ext)) {
          case Simple::Byte::ExtendedOpCode::CheckedArrayGetI32:
          case Simple::Byte::ExtendedOpCode::CheckedArrayGetI64:
          case Simple::Byte::ExtendedOpCode::CheckedArrayGetF32:
          case Simple::Byte::ExtendedOpCode::CheckedArrayGetF64:
          case Simple::Byte::ExtendedOpCode::CheckedArrayGetRef: {
            if (stack.size() < 2) { reason = "LLVM JIT CHECKED_ARRAY_GET underflow"; return false; }
            auto ext_op = static_cast<Simple::Byte::ExtendedOpCode>(ext);
            llvm::FunctionCallee helper = array_get_f32_helper;
            if (ext_op == Simple::Byte::ExtendedOpCode::CheckedArrayGetI32) helper = array_get_i32_helper;
            else if (ext_op == Simple::Byte::ExtendedOpCode::CheckedArrayGetI64) helper = array_get_i64_helper;
            else if (ext_op == Simple::Byte::ExtendedOpCode::CheckedArrayGetF64) helper = array_get_f64_helper;
            else if (ext_op == Simple::Byte::ExtendedOpCode::CheckedArrayGetRef) helper = array_get_ref_helper;
            llvm::Value* idx = to_slot(stack.back()); stack.pop_back();
            llvm::Value* ref = to_slot(stack.back()); stack.pop_back();
            stack.push_back(builder.CreateCall(helper, {ref, idx}));
            break;
          }
          case Simple::Byte::ExtendedOpCode::CheckedArraySetI32:
          case Simple::Byte::ExtendedOpCode::CheckedArraySetI64:
          case Simple::Byte::ExtendedOpCode::CheckedArraySetF32:
          case Simple::Byte::ExtendedOpCode::CheckedArraySetF64:
          case Simple::Byte::ExtendedOpCode::CheckedArraySetRef: {
            if (stack.size() < 3) { reason = "LLVM JIT CHECKED_ARRAY_SET underflow"; return false; }
            auto ext_op = static_cast<Simple::Byte::ExtendedOpCode>(ext);
            llvm::FunctionCallee helper = array_set_f32_helper;
            if (ext_op == Simple::Byte::ExtendedOpCode::CheckedArraySetI32) helper = array_set_i32_helper;
            else if (ext_op == Simple::Byte::ExtendedOpCode::CheckedArraySetI64) helper = array_set_i64_helper;
            else if (ext_op == Simple::Byte::ExtendedOpCode::CheckedArraySetF64) helper = array_set_f64_helper;
            else if (ext_op == Simple::Byte::ExtendedOpCode::CheckedArraySetRef) helper = array_set_ref_helper;
            llvm::Value* value = to_slot(stack.back()); stack.pop_back();
            llvm::Value* idx = to_slot(stack.back()); stack.pop_back();
            llvm::Value* ref = to_slot(stack.back()); stack.pop_back();
            builder.CreateCall(helper, {ref, idx, value});
            break;
          }
          case Simple::Byte::ExtendedOpCode::CheckedListGetI32:
          case Simple::Byte::ExtendedOpCode::CheckedListGetF32:
          case Simple::Byte::ExtendedOpCode::CheckedListGetRef:
          case Simple::Byte::ExtendedOpCode::CheckedListGetI64:
          case Simple::Byte::ExtendedOpCode::CheckedListGetF64: {
            if (stack.size() < 2) { reason = "LLVM JIT CHECKED_LIST_GET underflow"; return false; }
            auto ext_op = static_cast<Simple::Byte::ExtendedOpCode>(ext);
            llvm::FunctionCallee helper = (ext_op == Simple::Byte::ExtendedOpCode::CheckedListGetI64 ||
                                           ext_op == Simple::Byte::ExtendedOpCode::CheckedListGetF64)
                                              ? list_get64_helper
                                              : list_get32_helper;
            llvm::Value* idx = to_slot(stack.back()); stack.pop_back();
            llvm::Value* ref = to_slot(stack.back()); stack.pop_back();
            stack.push_back(builder.CreateCall(helper, {ref, idx}));
            break;
          }
          case Simple::Byte::ExtendedOpCode::CheckedListSetI32:
          case Simple::Byte::ExtendedOpCode::CheckedListSetF32:
          case Simple::Byte::ExtendedOpCode::CheckedListSetRef:
          case Simple::Byte::ExtendedOpCode::CheckedListSetI64:
          case Simple::Byte::ExtendedOpCode::CheckedListSetF64: {
            if (stack.size() < 3) { reason = "LLVM JIT CHECKED_LIST_SET underflow"; return false; }
            auto ext_op = static_cast<Simple::Byte::ExtendedOpCode>(ext);
            llvm::FunctionCallee helper = (ext_op == Simple::Byte::ExtendedOpCode::CheckedListSetI64 ||
                                           ext_op == Simple::Byte::ExtendedOpCode::CheckedListSetF64)
                                              ? list_set64_helper
                                              : list_set32_helper;
            llvm::Value* value = to_slot(stack.back()); stack.pop_back();
            llvm::Value* idx = to_slot(stack.back()); stack.pop_back();
            llvm::Value* ref = to_slot(stack.back()); stack.pop_back();
            builder.CreateCall(helper, {ref, idx, value});
            break;
          }
          case Simple::Byte::ExtendedOpCode::CheckedStringGetChar: {
            if (stack.size() < 2) { reason = "LLVM JIT CHECKED_STRING_GET_CHAR underflow"; return false; }
            llvm::Value* idx = to_slot(stack.back()); stack.pop_back();
            llvm::Value* ref = to_slot(stack.back()); stack.pop_back();
            stack.push_back(builder.CreateCall(string_get_char_helper, {ref, idx}));
            break;
          }
          case Simple::Byte::ExtendedOpCode::CheckedStringSlice: {
            if (stack.size() < 3) { reason = "LLVM JIT CHECKED_STRING_SLICE underflow"; return false; }
            llvm::Value* end = to_slot(stack.back()); stack.pop_back();
            llvm::Value* start = to_slot(stack.back()); stack.pop_back();
            llvm::Value* ref = to_slot(stack.back()); stack.pop_back();
            stack.push_back(builder.CreateCall(string_slice_helper, {ref, start, end}));
            break;
          }
          case Simple::Byte::ExtendedOpCode::InstanceOf: {
            if (stack.size() < 2) { reason = "LLVM JIT INSTANCEOF underflow"; return false; }
            llvm::Value* type = to_slot(stack.back()); stack.pop_back();
            llvm::Value* ref = to_slot(stack.back()); stack.pop_back();
            stack.push_back(builder.CreateCall(instance_of_helper, {ref, type}));
            break;
          }
          case Simple::Byte::ExtendedOpCode::CastRef: {
            if (stack.size() < 2) { reason = "LLVM JIT CAST_REF underflow"; return false; }
            stack.pop_back();
            llvm::Value* ref = to_slot(stack.back()); stack.pop_back();
            stack.push_back(builder.CreateCall(checked_ref_helper, {ref}));
            break;
          }
          case Simple::Byte::ExtendedOpCode::CheckedCastRef: {
            if (stack.size() < 2) { reason = "LLVM JIT CHECKED_CAST_REF underflow"; return false; }
            llvm::Value* type = to_slot(stack.back()); stack.pop_back();
            llvm::Value* ref = to_slot(stack.back()); stack.pop_back();
            stack.push_back(builder.CreateCall(checked_cast_ref_helper, {ref, type}));
            break;
          }
          case Simple::Byte::ExtendedOpCode::LoadVTable: {
            if (stack.empty()) { reason = "LLVM JIT LOAD_VTABLE underflow"; return false; }
            llvm::Value* ref = to_slot(stack.back()); stack.pop_back();
            stack.push_back(builder.CreateCall(type_of_helper, {ref}));
            break;
          }
          case Simple::Byte::ExtendedOpCode::LoadPtr: {
            if (stack.empty()) { reason = "LLVM JIT LOAD_PTR underflow"; return false; }
            llvm::Value* value = to_slot(stack.back());
            stack.pop_back();
            stack.push_back(value);
            break;
          }
          case Simple::Byte::ExtendedOpCode::StorePtr: {
            if (stack.size() < 2) { reason = "LLVM JIT STORE_PTR underflow"; return false; }
            stack.pop_back();
            stack.pop_back();
            break;
          }
          case Simple::Byte::ExtendedOpCode::MemCopy:
          case Simple::Byte::ExtendedOpCode::MemMove:
          case Simple::Byte::ExtendedOpCode::MemSet: {
            if (stack.size() < 3) { reason = "LLVM JIT memory op underflow"; return false; }
            stack.pop_back();
            stack.pop_back();
            stack.pop_back();
            break;
          }
          case Simple::Byte::ExtendedOpCode::MemCompare: {
            if (stack.size() < 3) { reason = "LLVM JIT MEM_COMPARE underflow"; return false; }
            stack.pop_back();
            stack.pop_back();
            stack.pop_back();
            stack.push_back(builder.getInt32(0));
            break;
          }
          case Simple::Byte::ExtendedOpCode::Throw:
          case Simple::Byte::ExtendedOpCode::Panic:
            builder.CreateCall(trap_helper, {});
            builder.CreateRet(builder.getInt64(0));
            stack.clear();
            break;
          case Simple::Byte::ExtendedOpCode::CaptureLocal:
          case Simple::Byte::ExtendedOpCode::CaptureRef:
          case Simple::Byte::ExtendedOpCode::AddressOfLocal:
          case Simple::Byte::ExtendedOpCode::AddressOfGlobal: {
            if (stack.empty()) { reason = "LLVM JIT ADDRESS_OF_* underflow"; return false; }
            llvm::Value* idx = to_i32(stack.back());
            stack.pop_back();
            auto ext_op = static_cast<Simple::Byte::ExtendedOpCode>(ext);
            const bool is_local = ext_op == Simple::Byte::ExtendedOpCode::AddressOfLocal ||
                                  ext_op == Simple::Byte::ExtendedOpCode::CaptureLocal ||
                                  ext_op == Simple::Byte::ExtendedOpCode::CaptureRef;
            const auto& slots = is_local ? locals : globals;
            llvm::Value* idx_neg = builder.CreateICmpSLT(idx, builder.getInt32(0));
            llvm::Value* idx_oob = builder.CreateICmpUGE(idx, builder.getInt32(static_cast<uint32_t>(slots.size())));
            emit_trap_if(builder.CreateOr(idx_neg, idx_oob), is_local ? "address.local" : "address.global");
            llvm::Value* selected = builder.getInt64(0);
            for (size_t i = 0; i < slots.size(); ++i) {
              llvm::Value* loaded = builder.CreateLoad(i64, slots[i]);
              selected = builder.CreateSelect(builder.CreateICmpEQ(idx, builder.getInt32(static_cast<uint32_t>(i))), loaded, selected);
            }
            stack.push_back(selected);
            break;
          }
          case Simple::Byte::ExtendedOpCode::Spawn:
          case Simple::Byte::ExtendedOpCode::MakeFuture: {
            if (stack.empty()) { reason = "LLVM JIT SPAWN/MAKE_FUTURE underflow"; return false; }
            llvm::Value* func_id = to_i32(stack.back());
            stack.pop_back();
            llvm::Value* id_neg = builder.CreateICmpSLT(func_id, builder.getInt32(0));
            llvm::Value* id_oob = builder.CreateICmpUGE(func_id, builder.getInt32(static_cast<uint32_t>(module.functions.size())));
            emit_trap_if(builder.CreateOr(id_neg, id_oob), "task.func.id");
            stack.push_back(func_id);
            break;
          }
          case Simple::Byte::ExtendedOpCode::EnumTag:
          case Simple::Byte::ExtendedOpCode::VariantTag:
          case Simple::Byte::ExtendedOpCode::IteratorHasNext:
          case Simple::Byte::ExtendedOpCode::ChannelRecv:
          case Simple::Byte::ExtendedOpCode::AtomicCompareExchange: {
            if (stack.empty()) { reason = "LLVM JIT pseudo op underflow"; return false; }
            if (static_cast<Simple::Byte::ExtendedOpCode>(ext) == Simple::Byte::ExtendedOpCode::AtomicCompareExchange) {
              if (stack.size() < 3) { reason = "LLVM JIT ATOMIC_CMPXCHG underflow"; return false; }
              stack.pop_back(); stack.pop_back(); stack.pop_back();
            } else {
              stack.pop_back();
            }
            stack.push_back(builder.getInt32(0));
            break;
          }
          case Simple::Byte::ExtendedOpCode::ResultIsOk:
          case Simple::Byte::ExtendedOpCode::ResultIsErr:
          case Simple::Byte::ExtendedOpCode::TryLock: {
            if (stack.empty()) { reason = "LLVM JIT true pseudo op underflow"; return false; }
            stack.pop_back();
            stack.push_back(builder.getInt32(1));
            break;
          }
          case Simple::Byte::ExtendedOpCode::EnumPayload:
          case Simple::Byte::ExtendedOpCode::EnumMake:
          case Simple::Byte::ExtendedOpCode::VariantPayload:
          case Simple::Byte::ExtendedOpCode::VariantMake:
          case Simple::Byte::ExtendedOpCode::RangeNew:
          case Simple::Byte::ExtendedOpCode::VecExtract: {
            if (stack.size() < 2) { reason = "LLVM JIT two-pop pseudo op underflow"; return false; }
            stack.pop_back();
            llvm::Value* value = stack.back();
            stack.pop_back();
            stack.push_back(value);
            break;
          }
          case Simple::Byte::ExtendedOpCode::RangeNewStep: {
            if (stack.size() < 3) { reason = "LLVM JIT RANGE_NEW_STEP underflow"; return false; }
            stack.pop_back();
            stack.pop_back();
            llvm::Value* start = stack.back();
            stack.pop_back();
            stack.push_back(start);
            break;
          }
          case Simple::Byte::ExtendedOpCode::ResultOk:
          case Simple::Byte::ExtendedOpCode::ResultErr:
          case Simple::Byte::ExtendedOpCode::ResultUnwrap:
          case Simple::Byte::ExtendedOpCode::ResultPropagateErr:
          case Simple::Byte::ExtendedOpCode::IteratorValue:
          case Simple::Byte::ExtendedOpCode::Join:
          case Simple::Byte::ExtendedOpCode::Await:
          case Simple::Byte::ExtendedOpCode::PollFuture:
          case Simple::Byte::ExtendedOpCode::AtomicLoad:
          case Simple::Byte::ExtendedOpCode::VecLoad:
          case Simple::Byte::ExtendedOpCode::VecSplat: {
            if (stack.empty()) { reason = "LLVM JIT identity pseudo op underflow"; return false; }
            llvm::Value* value = to_slot(stack.back());
            stack.pop_back();
            stack.push_back(value);
            break;
          }
          case Simple::Byte::ExtendedOpCode::RangeNext:
          case Simple::Byte::ExtendedOpCode::IteratorNext:
          case Simple::Byte::ExtendedOpCode::ChannelTryRecv: {
            if (stack.empty()) { reason = "LLVM JIT iterator/channel pseudo op underflow"; return false; }
            llvm::Value* value = to_slot(stack.back());
            stack.pop_back();
            stack.push_back(value);
            stack.push_back(builder.getInt32(0));
            break;
          }
          case Simple::Byte::ExtendedOpCode::Suspend:
            stack.push_back(builder.getInt32(0));
            break;
          case Simple::Byte::ExtendedOpCode::Detach:
          case Simple::Byte::ExtendedOpCode::Resume:
          case Simple::Byte::ExtendedOpCode::Lock:
          case Simple::Byte::ExtendedOpCode::Unlock:
          case Simple::Byte::ExtendedOpCode::Wait:
          case Simple::Byte::ExtendedOpCode::Notify:
          case Simple::Byte::ExtendedOpCode::NotifyAll:
          case Simple::Byte::ExtendedOpCode::SourceSpan:
          case Simple::Byte::ExtendedOpCode::Catch:
          case Simple::Byte::ExtendedOpCode::Finally:
          case Simple::Byte::ExtendedOpCode::Deopt:
          case Simple::Byte::ExtendedOpCode::Patchpoint:
          case Simple::Byte::ExtendedOpCode::InlineCache: {
            if (stack.empty()) { reason = "LLVM JIT one-pop pseudo op underflow"; return false; }
            stack.pop_back();
            break;
          }
          case Simple::Byte::ExtendedOpCode::ChannelSend:
          case Simple::Byte::ExtendedOpCode::AtomicStore:
          case Simple::Byte::ExtendedOpCode::VecStore: {
            if (stack.size() < 2) { reason = "LLVM JIT two-pop void pseudo op underflow"; return false; }
            stack.pop_back();
            stack.pop_back();
            break;
          }
          case Simple::Byte::ExtendedOpCode::AtomicAdd:
          case Simple::Byte::ExtendedOpCode::AtomicSub:
          case Simple::Byte::ExtendedOpCode::VecAdd:
          case Simple::Byte::ExtendedOpCode::VecSub:
          case Simple::Byte::ExtendedOpCode::VecMul:
          case Simple::Byte::ExtendedOpCode::VecDiv:
          case Simple::Byte::ExtendedOpCode::VecAnd:
          case Simple::Byte::ExtendedOpCode::VecOr:
          case Simple::Byte::ExtendedOpCode::VecXor: {
            if (stack.size() < 2) { reason = "LLVM JIT binary pseudo op underflow"; return false; }
            llvm::Value* rhs = to_slot(stack.back()); stack.pop_back();
            llvm::Value* lhs = to_slot(stack.back()); stack.pop_back();
            llvm::Value* result = nullptr;
            auto ext_op = static_cast<Simple::Byte::ExtendedOpCode>(ext);
            if (ext_op == Simple::Byte::ExtendedOpCode::AtomicAdd || ext_op == Simple::Byte::ExtendedOpCode::VecAdd) result = builder.CreateAdd(lhs, rhs);
            if (ext_op == Simple::Byte::ExtendedOpCode::AtomicSub || ext_op == Simple::Byte::ExtendedOpCode::VecSub) result = builder.CreateSub(lhs, rhs);
            if (ext_op == Simple::Byte::ExtendedOpCode::VecMul) result = builder.CreateMul(lhs, rhs);
            if (ext_op == Simple::Byte::ExtendedOpCode::VecAnd) result = builder.CreateAnd(lhs, rhs);
            if (ext_op == Simple::Byte::ExtendedOpCode::VecOr) result = builder.CreateOr(lhs, rhs);
            if (ext_op == Simple::Byte::ExtendedOpCode::VecXor) result = builder.CreateXor(lhs, rhs);
            if (ext_op == Simple::Byte::ExtendedOpCode::VecDiv) {
              emit_trap_if(builder.CreateICmpEQ(rhs, builder.getInt64(0)), "vec.div.zero");
              result = builder.CreateUDiv(lhs, rhs);
            }
            stack.push_back(result);
            break;
          }
          case Simple::Byte::ExtendedOpCode::PtrIsNull: {
            if (stack.empty()) { reason = "LLVM JIT PTR_IS_NULL underflow"; return false; }
            llvm::Value* ptr = to_slot(stack.back());
            stack.pop_back();
            stack.push_back(builder.CreateZExt(builder.CreateICmpEQ(to_i32(ptr), builder.getInt32(Simple::VM::HeapLayout::kNullRef)), i32));
            break;
          }
          case Simple::Byte::ExtendedOpCode::PtrCheckNull: {
            if (stack.empty()) { reason = "LLVM JIT PTR_CHECK_NULL underflow"; return false; }
            llvm::Value* ptr = to_slot(stack.back());
            stack.pop_back();
            emit_trap_if(builder.CreateICmpEQ(to_i32(ptr), builder.getInt32(Simple::VM::HeapLayout::kNullRef)), "ptr.check.null");
            stack.push_back(ptr);
            break;
          }
          case Simple::Byte::ExtendedOpCode::PtrAdd:
          case Simple::Byte::ExtendedOpCode::PtrOffset: {
            if (stack.size() < 2) { reason = "LLVM JIT PTR_ADD/OFFSET underflow"; return false; }
            llvm::Value* rhs = to_i32(stack.back()); stack.pop_back();
            llvm::Value* lhs = to_i32(stack.back()); stack.pop_back();
            stack.push_back(builder.CreateAdd(lhs, rhs));
            break;
          }
          case Simple::Byte::ExtendedOpCode::PtrEq:
          case Simple::Byte::ExtendedOpCode::PtrNe: {
            if (stack.size() < 2) { reason = "LLVM JIT PTR_EQ/NE underflow"; return false; }
            llvm::Value* rhs = to_slot(stack.back()); stack.pop_back();
            llvm::Value* lhs = to_slot(stack.back()); stack.pop_back();
            llvm::Value* eq = builder.CreateICmpEQ(lhs, rhs);
            if (static_cast<Simple::Byte::ExtendedOpCode>(ext) == Simple::Byte::ExtendedOpCode::PtrNe) eq = builder.CreateNot(eq);
            stack.push_back(builder.CreateZExt(eq, i32));
            break;
          }
          case Simple::Byte::ExtendedOpCode::PtrCheckBounds:
          case Simple::Byte::ExtendedOpCode::GuardBounds: {
            if (stack.size() < 3) {
              reason = "LLVM JIT GUARD_BOUNDS underflow";
              return false;
            }
            llvm::Value* length = to_i32(stack.back());
            stack.pop_back();
            llvm::Value* index = to_i32(stack.back());
            stack.pop_back();
            llvm::Value* value = stack.back();
            stack.pop_back();
            llvm::Value* index_neg = builder.CreateICmpSLT(index, builder.getInt32(0));
            llvm::Value* length_neg = builder.CreateICmpSLT(length, builder.getInt32(0));
            llvm::Value* index_oob = builder.CreateICmpSGE(index, length);
            emit_trap_if(builder.CreateOr(builder.CreateOr(index_neg, length_neg), index_oob),
                         static_cast<Simple::Byte::ExtendedOpCode>(ext) == Simple::Byte::ExtendedOpCode::GuardBounds
                             ? "guard.bounds"
                             : "ptr.check.bounds");
            stack.push_back(value);
            break;
          }
          case Simple::Byte::ExtendedOpCode::CheckedConvI32ToI64: {
            if (stack.empty()) {
              reason = "LLVM JIT checked conv underflow";
              return false;
            }
            llvm::Value* value = to_i32(stack.back());
            stack.pop_back();
            stack.push_back(builder.CreateSExt(value, i64));
            break;
          }
          case Simple::Byte::ExtendedOpCode::CheckedConvI64ToI32: {
            if (stack.empty()) {
              reason = "LLVM JIT checked conv underflow";
              return false;
            }
            llvm::Value* value = to_slot(stack.back());
            stack.pop_back();
            llvm::Value* too_low = builder.CreateICmpSLT(value, builder.getInt64(static_cast<uint64_t>(INT32_MIN)));
            llvm::Value* too_high = builder.CreateICmpSGT(value, builder.getInt64(INT32_MAX));
            emit_trap_if(builder.CreateOr(too_low, too_high), "checked.conv.i64.i32");
            stack.push_back(builder.CreateTrunc(value, i32));
            break;
          }
          case Simple::Byte::ExtendedOpCode::CheckedConvI32ToF32: {
            if (stack.empty()) { reason = "LLVM JIT checked conv underflow"; return false; }
            llvm::Value* value = to_i32(stack.back()); stack.pop_back();
            stack.push_back(f32_to_slot_bits(builder.CreateSIToFP(value, f32)));
            break;
          }
          case Simple::Byte::ExtendedOpCode::CheckedConvI32ToF64: {
            if (stack.empty()) { reason = "LLVM JIT checked conv underflow"; return false; }
            llvm::Value* value = to_i32(stack.back()); stack.pop_back();
            stack.push_back(f64_to_slot_bits(builder.CreateSIToFP(value, f64)));
            break;
          }
          case Simple::Byte::ExtendedOpCode::CheckedConvF32ToI32: {
            if (stack.empty()) { reason = "LLVM JIT checked conv underflow"; return false; }
            llvm::Value* value = to_f32(stack.back()); stack.pop_back();
            llvm::Value* unordered = builder.CreateFCmpUNO(value, value);
            llvm::Value* too_low = builder.CreateFCmpOLT(value, llvm::ConstantFP::get(f32, static_cast<float>(INT32_MIN)));
            llvm::Value* too_high = builder.CreateFCmpOGT(value, llvm::ConstantFP::get(f32, static_cast<float>(INT32_MAX)));
            emit_trap_if(builder.CreateOr(unordered, builder.CreateOr(too_low, too_high)), "checked.conv.f32.i32");
            stack.push_back(builder.CreateFPToSI(value, i32));
            break;
          }
          case Simple::Byte::ExtendedOpCode::CheckedConvF64ToI32: {
            if (stack.empty()) { reason = "LLVM JIT checked conv underflow"; return false; }
            llvm::Value* value = to_f64(stack.back()); stack.pop_back();
            llvm::Value* unordered = builder.CreateFCmpUNO(value, value);
            llvm::Value* too_low = builder.CreateFCmpOLT(value, llvm::ConstantFP::get(f64, static_cast<double>(INT32_MIN)));
            llvm::Value* too_high = builder.CreateFCmpOGT(value, llvm::ConstantFP::get(f64, static_cast<double>(INT32_MAX)));
            emit_trap_if(builder.CreateOr(unordered, builder.CreateOr(too_low, too_high)), "checked.conv.f64.i32");
            stack.push_back(builder.CreateFPToSI(value, i32));
            break;
          }
          case Simple::Byte::ExtendedOpCode::CheckedConvF32ToF64: {
            if (stack.empty()) { reason = "LLVM JIT checked conv underflow"; return false; }
            llvm::Value* value = to_f32(stack.back()); stack.pop_back();
            stack.push_back(f64_to_slot_bits(builder.CreateFPExt(value, f64)));
            break;
          }
          case Simple::Byte::ExtendedOpCode::CheckedConvF64ToF32: {
            if (stack.empty()) { reason = "LLVM JIT checked conv underflow"; return false; }
            llvm::Value* value = to_f64(stack.back()); stack.pop_back();
            llvm::Value* finite = builder.CreateAnd(
                builder.CreateFCmpORD(value, value),
                builder.CreateAnd(
                    builder.CreateFCmpOGE(value, llvm::ConstantFP::get(f64, -std::numeric_limits<double>::max())),
                    builder.CreateFCmpOLE(value, llvm::ConstantFP::get(f64, std::numeric_limits<double>::max()))));
            llvm::Value* out_of_range = builder.CreateOr(
                builder.CreateFCmpOLT(value, llvm::ConstantFP::get(f64, -static_cast<double>(std::numeric_limits<float>::max()))),
                builder.CreateFCmpOGT(value, llvm::ConstantFP::get(f64, static_cast<double>(std::numeric_limits<float>::max()))));
            emit_trap_if(builder.CreateAnd(finite, out_of_range), "checked.conv.f64.f32");
            stack.push_back(f32_to_slot_bits(builder.CreateFPTrunc(value, f32)));
            break;
          }
          default: {
            if (stack.size() < 2) {
              reason = "LLVM JIT checked binary underflow";
              return false;
            }
            llvm::Value* rhs_raw = stack.back(); stack.pop_back();
            llvm::Value* lhs_raw = stack.back(); stack.pop_back();
            llvm::Value* rhs = to_i32(rhs_raw);
            llvm::Value* lhs = to_i32(lhs_raw);
            llvm::Value* result = nullptr;
            switch (static_cast<Simple::Byte::ExtendedOpCode>(ext)) {
          case Simple::Byte::ExtendedOpCode::CheckedAddI32: {
            llvm::Value* lhs64 = builder.CreateSExt(lhs, i64);
            llvm::Value* rhs64 = builder.CreateSExt(rhs, i64);
            llvm::Value* wide = builder.CreateAdd(lhs64, rhs64);
            emit_trap_if(builder.CreateOr(builder.CreateICmpSLT(wide, builder.getInt64(static_cast<uint64_t>(INT32_MIN))),
                                          builder.CreateICmpSGT(wide, builder.getInt64(INT32_MAX))), "checked.add.i32");
            result = builder.CreateTrunc(wide, i32);
            break;
          }
          case Simple::Byte::ExtendedOpCode::CheckedSubI32: {
            llvm::Value* lhs64 = builder.CreateSExt(lhs, i64);
            llvm::Value* rhs64 = builder.CreateSExt(rhs, i64);
            llvm::Value* wide = builder.CreateSub(lhs64, rhs64);
            emit_trap_if(builder.CreateOr(builder.CreateICmpSLT(wide, builder.getInt64(static_cast<uint64_t>(INT32_MIN))),
                                          builder.CreateICmpSGT(wide, builder.getInt64(INT32_MAX))), "checked.sub.i32");
            result = builder.CreateTrunc(wide, i32);
            break;
          }
          case Simple::Byte::ExtendedOpCode::CheckedMulI32: {
            llvm::Value* lhs64 = builder.CreateSExt(lhs, i64);
            llvm::Value* rhs64 = builder.CreateSExt(rhs, i64);
            llvm::Value* wide = builder.CreateMul(lhs64, rhs64);
            emit_trap_if(builder.CreateOr(builder.CreateICmpSLT(wide, builder.getInt64(static_cast<uint64_t>(INT32_MIN))),
                                          builder.CreateICmpSGT(wide, builder.getInt64(INT32_MAX))), "checked.mul.i32");
            result = builder.CreateTrunc(wide, i32);
            break;
          }
          case Simple::Byte::ExtendedOpCode::CheckedDivI32:
          case Simple::Byte::ExtendedOpCode::CheckedModI32: {
            llvm::Value* div_zero = builder.CreateICmpEQ(rhs, builder.getInt32(0));
            llvm::Value* overflow = builder.CreateAnd(builder.CreateICmpEQ(lhs, builder.getInt32(INT32_MIN)),
                                                      builder.CreateICmpEQ(rhs, builder.getInt32(-1)));
            emit_trap_if(builder.CreateOr(div_zero, overflow), "checked.divmod.i32");
            result = (static_cast<Simple::Byte::ExtendedOpCode>(ext) == Simple::Byte::ExtendedOpCode::CheckedDivI32)
                         ? builder.CreateSDiv(lhs, rhs)
                         : builder.CreateSRem(lhs, rhs);
            break;
          }
          case Simple::Byte::ExtendedOpCode::CheckedAddU32: {
            llvm::Value* lhs64 = builder.CreateZExt(lhs, i64);
            llvm::Value* rhs64 = builder.CreateZExt(rhs, i64);
            llvm::Value* wide = builder.CreateAdd(lhs64, rhs64);
            emit_trap_if(builder.CreateICmpUGT(wide, builder.getInt64(UINT32_MAX)), "checked.add.u32");
            result = builder.CreateTrunc(wide, i32);
            break;
          }
          case Simple::Byte::ExtendedOpCode::CheckedSubU32: {
            emit_trap_if(builder.CreateICmpULT(lhs, rhs), "checked.sub.u32");
            result = builder.CreateSub(lhs, rhs);
            break;
          }
          case Simple::Byte::ExtendedOpCode::CheckedMulU32: {
            llvm::Value* lhs64 = builder.CreateZExt(lhs, i64);
            llvm::Value* rhs64 = builder.CreateZExt(rhs, i64);
            llvm::Value* wide = builder.CreateMul(lhs64, rhs64);
            emit_trap_if(builder.CreateICmpUGT(wide, builder.getInt64(UINT32_MAX)), "checked.mul.u32");
            result = builder.CreateTrunc(wide, i32);
            break;
          }
          case Simple::Byte::ExtendedOpCode::CheckedDivU32:
          case Simple::Byte::ExtendedOpCode::CheckedModU32: {
            emit_trap_if(builder.CreateICmpEQ(rhs, builder.getInt32(0)), "checked.divmod.u32");
            result = (static_cast<Simple::Byte::ExtendedOpCode>(ext) == Simple::Byte::ExtendedOpCode::CheckedDivU32)
                         ? builder.CreateUDiv(lhs, rhs)
                         : builder.CreateURem(lhs, rhs);
            break;
          }
          case Simple::Byte::ExtendedOpCode::CheckedAddI64:
          case Simple::Byte::ExtendedOpCode::CheckedSubI64:
          case Simple::Byte::ExtendedOpCode::CheckedMulI64: {
            llvm::Value* lhs64 = to_slot(lhs_raw);
            llvm::Value* rhs64 = to_slot(rhs_raw);
            llvm::Intrinsic::ID id = llvm::Intrinsic::sadd_with_overflow;
            const char* name = "checked.add.i64";
            if (static_cast<Simple::Byte::ExtendedOpCode>(ext) == Simple::Byte::ExtendedOpCode::CheckedSubI64) {
              id = llvm::Intrinsic::ssub_with_overflow;
              name = "checked.sub.i64";
            } else if (static_cast<Simple::Byte::ExtendedOpCode>(ext) == Simple::Byte::ExtendedOpCode::CheckedMulI64) {
              id = llvm::Intrinsic::smul_with_overflow;
              name = "checked.mul.i64";
            }
            llvm::Value* pair = builder.CreateIntrinsic(id, {i64}, {lhs64, rhs64});
            result = builder.CreateExtractValue(pair, {0});
            emit_trap_if(builder.CreateExtractValue(pair, {1}), name);
            break;
          }
          case Simple::Byte::ExtendedOpCode::CheckedDivI64:
          case Simple::Byte::ExtendedOpCode::CheckedModI64: {
            llvm::Value* lhs64 = to_slot(lhs_raw);
            llvm::Value* rhs64 = to_slot(rhs_raw);
            llvm::Value* div_zero = builder.CreateICmpEQ(rhs64, builder.getInt64(0));
            llvm::Value* overflow = builder.CreateAnd(builder.CreateICmpEQ(lhs64, builder.getInt64(static_cast<uint64_t>(INT64_MIN))),
                                                      builder.CreateICmpEQ(rhs64, builder.getInt64(static_cast<uint64_t>(-1))));
            emit_trap_if(builder.CreateOr(div_zero, overflow), "checked.divmod.i64");
            result = (static_cast<Simple::Byte::ExtendedOpCode>(ext) == Simple::Byte::ExtendedOpCode::CheckedDivI64)
                         ? builder.CreateSDiv(lhs64, rhs64)
                         : builder.CreateSRem(lhs64, rhs64);
            break;
          }
          case Simple::Byte::ExtendedOpCode::CheckedAddU64:
          case Simple::Byte::ExtendedOpCode::CheckedSubU64:
          case Simple::Byte::ExtendedOpCode::CheckedMulU64: {
            llvm::Value* lhs64 = to_slot(lhs_raw);
            llvm::Value* rhs64 = to_slot(rhs_raw);
            llvm::Intrinsic::ID id = llvm::Intrinsic::uadd_with_overflow;
            const char* name = "checked.add.u64";
            if (static_cast<Simple::Byte::ExtendedOpCode>(ext) == Simple::Byte::ExtendedOpCode::CheckedSubU64) {
              id = llvm::Intrinsic::usub_with_overflow;
              name = "checked.sub.u64";
            } else if (static_cast<Simple::Byte::ExtendedOpCode>(ext) == Simple::Byte::ExtendedOpCode::CheckedMulU64) {
              id = llvm::Intrinsic::umul_with_overflow;
              name = "checked.mul.u64";
            }
            llvm::Value* pair = builder.CreateIntrinsic(id, {i64}, {lhs64, rhs64});
            result = builder.CreateExtractValue(pair, {0});
            emit_trap_if(builder.CreateExtractValue(pair, {1}), name);
            break;
          }
          case Simple::Byte::ExtendedOpCode::CheckedDivU64:
          case Simple::Byte::ExtendedOpCode::CheckedModU64: {
            llvm::Value* lhs64 = to_slot(lhs_raw);
            llvm::Value* rhs64 = to_slot(rhs_raw);
            emit_trap_if(builder.CreateICmpEQ(rhs64, builder.getInt64(0)), "checked.divmod.u64");
            result = (static_cast<Simple::Byte::ExtendedOpCode>(ext) == Simple::Byte::ExtendedOpCode::CheckedDivU64)
                         ? builder.CreateUDiv(lhs64, rhs64)
                         : builder.CreateURem(lhs64, rhs64);
            break;
          }
              default:
                reason = std::string("unsupported: checked arithmetic extended opcode ") + std::to_string(ext);
                return false;
            }
            stack.push_back(result);
            break;
          }
        }
        break;
      }
      case OpCode::CheckCapability:
      case OpCode::EnterSandbox:
      case OpCode::InitGlobal:
      case OpCode::InitModule:
      case OpCode::EnsureModuleInit:
      case OpCode::TraceEnter:
      case OpCode::TraceLeave:
        ReadU32(module.code, pc);
        break;
      case OpCode::Intrinsic: {
        uint32_t id = ReadU32(module.code, pc);
        if (skipping_unreachable) break;
        switch (id) {
          case Simple::VM::kIntrinsicTrap:
            if (stack.empty()) { reason = "LLVM JIT intrinsic trap underflow"; return false; }
            stack.pop_back();
            builder.CreateCall(trap_helper, {});
            builder.CreateRet(builder.getInt64(0));
            stack.clear();
            break;
          case Simple::VM::kIntrinsicBreakpoint:
            break;
          case Simple::VM::kIntrinsicLogI32:
          case Simple::VM::kIntrinsicLogI64:
          case Simple::VM::kIntrinsicLogF32:
          case Simple::VM::kIntrinsicLogF64:
          case Simple::VM::kIntrinsicLogRef:
            if (stack.empty()) { reason = "LLVM JIT intrinsic log underflow"; return false; }
            stack.pop_back();
            break;
          case Simple::VM::kIntrinsicPrintAny: {
            if (stack.size() < 2) { reason = "LLVM JIT print_any underflow"; return false; }
            llvm::Value* tag = to_i32(stack.back()); stack.pop_back();
            llvm::Value* value = to_slot(stack.back()); stack.pop_back();
            builder.CreateCall(print_any_helper, {value, tag});
            break;
          }
          case Simple::VM::kIntrinsicWriteStdout:
          case Simple::VM::kIntrinsicWriteStderr:
            if (stack.size() < 2) { reason = "LLVM JIT write intrinsic underflow"; return false; }
            stack.pop_back();
            stack.pop_back();
            break;
          case Simple::VM::kIntrinsicMonoNs:
            stack.push_back(builder.CreateCall(mono_ns_helper, {}));
            break;
          case Simple::VM::kIntrinsicWallNs:
            stack.push_back(builder.CreateCall(wall_ns_helper, {}));
            break;
          case Simple::VM::kIntrinsicRandU32:
            stack.push_back(builder.getInt64(0));
            break;
          case Simple::VM::kIntrinsicRandU64:
            stack.push_back(builder.getInt64(0));
            break;
          case Simple::VM::kIntrinsicAbsI32: {
            if (stack.empty()) { reason = "LLVM JIT abs_i32 underflow"; return false; }
            llvm::Value* value = to_i32(stack.back()); stack.pop_back();
            llvm::Value* is_neg = builder.CreateICmpSLT(value, builder.getInt32(0));
            stack.push_back(builder.CreateSelect(is_neg, builder.CreateNeg(value), value));
            break;
          }
          case Simple::VM::kIntrinsicAbsI64: {
            if (stack.empty()) { reason = "LLVM JIT abs_i64 underflow"; return false; }
            llvm::Value* value = to_slot(stack.back()); stack.pop_back();
            llvm::Value* is_neg = builder.CreateICmpSLT(value, builder.getInt64(0));
            stack.push_back(builder.CreateSelect(is_neg, builder.CreateNeg(value), value));
            break;
          }
          case Simple::VM::kIntrinsicMinI32:
          case Simple::VM::kIntrinsicMaxI32: {
            if (stack.size() < 2) { reason = "LLVM JIT min/max i32 underflow"; return false; }
            llvm::Value* rhs = to_i32(stack.back()); stack.pop_back();
            llvm::Value* lhs = to_i32(stack.back()); stack.pop_back();
            llvm::Value* cmp = (id == Simple::VM::kIntrinsicMinI32) ? builder.CreateICmpSLT(lhs, rhs)
                                                                    : builder.CreateICmpSGT(lhs, rhs);
            stack.push_back(builder.CreateSelect(cmp, lhs, rhs));
            break;
          }
          case Simple::VM::kIntrinsicMinI64:
          case Simple::VM::kIntrinsicMaxI64: {
            if (stack.size() < 2) { reason = "LLVM JIT min/max i64 underflow"; return false; }
            llvm::Value* rhs = to_slot(stack.back()); stack.pop_back();
            llvm::Value* lhs = to_slot(stack.back()); stack.pop_back();
            llvm::Value* cmp = (id == Simple::VM::kIntrinsicMinI64) ? builder.CreateICmpSLT(lhs, rhs)
                                                                    : builder.CreateICmpSGT(lhs, rhs);
            stack.push_back(builder.CreateSelect(cmp, lhs, rhs));
            break;
          }
          case Simple::VM::kIntrinsicMinF32:
          case Simple::VM::kIntrinsicMaxF32: {
            if (stack.size() < 2) { reason = "LLVM JIT min/max f32 underflow"; return false; }
            llvm::Value* rhs = to_f32(stack.back()); stack.pop_back();
            llvm::Value* lhs = to_f32(stack.back()); stack.pop_back();
            llvm::Value* cmp = (id == Simple::VM::kIntrinsicMinF32) ? builder.CreateFCmpOLT(lhs, rhs)
                                                                    : builder.CreateFCmpOGT(lhs, rhs);
            stack.push_back(f32_to_slot_bits(builder.CreateSelect(cmp, lhs, rhs)));
            break;
          }
          case Simple::VM::kIntrinsicMinF64:
          case Simple::VM::kIntrinsicMaxF64: {
            if (stack.size() < 2) { reason = "LLVM JIT min/max f64 underflow"; return false; }
            llvm::Value* rhs = to_f64(stack.back()); stack.pop_back();
            llvm::Value* lhs = to_f64(stack.back()); stack.pop_back();
            llvm::Value* cmp = (id == Simple::VM::kIntrinsicMinF64) ? builder.CreateFCmpOLT(lhs, rhs)
                                                                    : builder.CreateFCmpOGT(lhs, rhs);
            stack.push_back(f64_to_slot_bits(builder.CreateSelect(cmp, lhs, rhs)));
            break;
          }
          case Simple::VM::kIntrinsicSqrtF32: {
            if (stack.empty()) { reason = "LLVM JIT sqrt f32 underflow"; return false; }
            llvm::Value* value = to_f32(stack.back()); stack.pop_back();
            llvm::Function* sqrt_fn = llvm::Intrinsic::getDeclaration(ir_module.get(), llvm::Intrinsic::sqrt, {f32});
            stack.push_back(f32_to_slot_bits(builder.CreateCall(sqrt_fn, {value})));
            break;
          }
          case Simple::VM::kIntrinsicSqrtF64: {
            if (stack.empty()) { reason = "LLVM JIT sqrt f64 underflow"; return false; }
            llvm::Value* value = to_f64(stack.back()); stack.pop_back();
            llvm::Function* sqrt_fn = llvm::Intrinsic::getDeclaration(ir_module.get(), llvm::Intrinsic::sqrt, {f64});
            stack.push_back(f64_to_slot_bits(builder.CreateCall(sqrt_fn, {value})));
            break;
          }
          default:
            reason = std::string("unsupported: intrinsic ") + std::to_string(id);
            return false;
        }
        break;
      }
      case OpCode::Line:
        ReadU32(module.code, pc);
        ReadU32(module.code, pc);
        break;
      case OpCode::ProfileStart:
      case OpCode::ProfileEnd:
        ReadU32(module.code, pc);
        break;
      case OpCode::Dup:
        if (skipping_unreachable) break;
        if (stack.empty()) { reason = "LLVM JIT DUP underflow"; return false; }
        stack.push_back(stack.back());
        break;
      case OpCode::Dup2:
        if (skipping_unreachable) break;
        if (stack.size() < 2) { reason = "LLVM JIT DUP2 underflow"; return false; }
        stack.push_back(stack[stack.size() - 2]);
        stack.push_back(stack[stack.size() - 2]);
        break;
      case OpCode::Swap:
        if (skipping_unreachable) break;
        if (stack.size() < 2) { reason = "LLVM JIT SWAP underflow"; return false; }
        std::swap(stack[stack.size() - 1], stack[stack.size() - 2]);
        break;
      case OpCode::Rot: {
        if (skipping_unreachable) break;
        if (stack.size() < 3) { reason = "LLVM JIT ROT underflow"; return false; }
        llvm::Value* c = stack[stack.size() - 1];
        llvm::Value* b = stack[stack.size() - 2];
        llvm::Value* a = stack[stack.size() - 3];
        stack[stack.size() - 3] = b;
        stack[stack.size() - 2] = c;
        stack[stack.size() - 1] = a;
        break;
      }
      case OpCode::Enter: {
        uint16_t count = ReadU16(module.code, pc);
        locals.clear();
        globals.clear();
        local_type_ids.assign(count, kUnknownJitTypeId);
        locals.reserve(count);
        globals.reserve(module.globals.size());
        for (uint16_t i = 0; i < count; ++i) {
          llvm::AllocaInst* slot = create_entry_alloca(i64, nullptr, "local" + std::to_string(i));
          builder.CreateStore(builder.getInt64(0), slot);
          locals.push_back(slot);
        }
        for (size_t i = 0; i < module.globals.size(); ++i) {
          llvm::AllocaInst* slot = create_entry_alloca(i64, nullptr, "global" + std::to_string(i));
          uint64_t initial = 0;
          uint32_t const_id = module.globals[i].init_const_id;
          if (const_id != 0xFFFFFFFFu) {
            uint32_t kind = read_const_u32(const_id);
            if (kind == 3) {
              initial = static_cast<uint64_t>(read_const_u32(const_id + 4));
            } else if (kind == 4) {
              initial = read_const_u64(const_id + 4);
            }
          }
          builder.CreateStore(builder.getInt64(initial), slot);
          globals.push_back(slot);
        }
        auto arg_it = fn->arg_begin();
        llvm::Value* arg_slots = &*arg_it++;
        for (uint16_t i = 0; i < param_count; ++i) {
          llvm::Value* offset = builder.getInt64(i);
          llvm::Value* ptr = builder.CreateGEP(i64, arg_slots, offset);
          llvm::Value* loaded_arg = builder.CreateLoad(i64, ptr);
          builder.CreateStore(loaded_arg, locals[i]);
          if (sig.param_type_start + i < module.param_types.size()) {
            local_type_ids[i] = module.param_types[sig.param_type_start + i];
            note_value_type(loaded_arg, local_type_ids[i]);
          }
        }
        break;
      }
      case OpCode::ConstI8: {
        int8_t value = static_cast<int8_t>(ReadU8(module.code, pc));
        if (!skipping_unreachable) stack.push_back(builder.getInt32(value));
        break;
      }
      case OpCode::ConstU8: {
        uint8_t value = ReadU8(module.code, pc);
        if (!skipping_unreachable) stack.push_back(builder.getInt32(value));
        break;
      }
      case OpCode::ConstI16: {
        int16_t value = static_cast<int16_t>(ReadU16(module.code, pc));
        if (!skipping_unreachable) stack.push_back(builder.getInt32(value));
        break;
      }
      case OpCode::ConstU16:
      case OpCode::ConstChar: {
        uint16_t value = ReadU16(module.code, pc);
        if (!skipping_unreachable) stack.push_back(builder.getInt32(value));
        break;
      }
      case OpCode::ConstI32: {
        int32_t value = ReadI32(module.code, pc);
        if (!skipping_unreachable) stack.push_back(builder.getInt32(value));
        break;
      }
      case OpCode::ConstI64: {
        int64_t value = ReadI64(module.code, pc);
        if (!skipping_unreachable) stack.push_back(builder.getInt64(static_cast<uint64_t>(value)));
        break;
      }
      case OpCode::ConstU32: {
        uint32_t value = ReadU32(module.code, pc);
        if (!skipping_unreachable) stack.push_back(builder.getInt32(value));
        break;
      }
      case OpCode::ConstU64: {
        uint64_t value = ReadU64(module.code, pc);
        if (!skipping_unreachable) stack.push_back(builder.getInt64(value));
        break;
      }
      case OpCode::ConstString: {
        uint32_t const_id = ReadU32(module.code, pc);
        if (!skipping_unreachable) stack.push_back(builder.CreateCall(const_string_helper, {builder.getInt32(const_id)}));
        break;
      }
      case OpCode::ConstI128:
      case OpCode::ConstU128:
        ReadU32(module.code, pc);
        if (!skipping_unreachable) stack.push_back(builder.getInt64(Simple::VM::HeapLayout::kNullRef));
        break;
      case OpCode::ConstNull:
        if (!skipping_unreachable) stack.push_back(builder.getInt64(Simple::VM::HeapLayout::kNullRef));
        break;
      case OpCode::StackTrace:
        if (!skipping_unreachable) stack.push_back(builder.CreateCall(stack_trace_helper, {}));
        break;
      case OpCode::ConstBool: {
        uint8_t value = ReadU8(module.code, pc);
        if (!skipping_unreachable) stack.push_back(builder.getInt32(value ? 1 : 0));
        break;
      }
      case OpCode::ConstF32: {
        uint32_t bits = ReadU32(module.code, pc);
        if (!skipping_unreachable) stack.push_back(builder.getInt32(bits));
        break;
      }
      case OpCode::ConstF64: {
        uint64_t bits = ReadU64(module.code, pc);
        if (!skipping_unreachable) stack.push_back(builder.getInt64(bits));
        break;
      }
      case OpCode::AddI32:
      case OpCode::SubI32:
      case OpCode::MulI32:
      case OpCode::DivI32:
      case OpCode::ModI32:
      case OpCode::AndI32:
      case OpCode::OrI32:
      case OpCode::XorI32:
      case OpCode::ShlI32:
      case OpCode::ShrI32:
      case OpCode::AddU32:
      case OpCode::SubU32:
      case OpCode::MulU32:
      case OpCode::DivU32:
      case OpCode::ModU32: {
        if (skipping_unreachable) break;
        if (stack.size() < 2) {
          reason = "LLVM JIT stack underflow";
          return false;
        }
        llvm::Value* rhs = to_i32(stack.back());
        stack.pop_back();
        llvm::Value* lhs = to_i32(stack.back());
        stack.pop_back();
        llvm::Value* value = nullptr;
        if (op == OpCode::AddI32 || op == OpCode::AddU32) value = builder.CreateAdd(lhs, rhs);
        if (op == OpCode::SubI32 || op == OpCode::SubU32) value = builder.CreateSub(lhs, rhs);
        if (op == OpCode::MulI32 || op == OpCode::MulU32) value = builder.CreateMul(lhs, rhs);
        if (op == OpCode::DivI32 || op == OpCode::ModI32 || op == OpCode::DivU32 || op == OpCode::ModU32) {
          llvm::Value* is_zero = builder.CreateICmpEQ(rhs, builder.getInt32(0));
          llvm::Value* safe_rhs = builder.CreateSelect(is_zero, builder.getInt32(1), rhs);
          if (op == OpCode::DivI32) value = builder.CreateSDiv(lhs, safe_rhs);
          if (op == OpCode::ModI32) value = builder.CreateSRem(lhs, safe_rhs);
          if (op == OpCode::DivU32) value = builder.CreateUDiv(lhs, safe_rhs);
          if (op == OpCode::ModU32) value = builder.CreateURem(lhs, safe_rhs);
          value = builder.CreateSelect(is_zero, builder.getInt32(0), value);
        }
        if (op == OpCode::AndI32) value = builder.CreateAnd(lhs, rhs);
        if (op == OpCode::OrI32) value = builder.CreateOr(lhs, rhs);
        if (op == OpCode::XorI32) value = builder.CreateXor(lhs, rhs);
        if (op == OpCode::ShlI32) value = builder.CreateShl(lhs, builder.CreateAnd(rhs, builder.getInt32(31)));
        if (op == OpCode::ShrI32) value = builder.CreateLShr(lhs, builder.CreateAnd(rhs, builder.getInt32(31)));
        stack.push_back(value);
        break;
      }
      case OpCode::IncI8:
      case OpCode::DecI8:
      case OpCode::IncI16:
      case OpCode::DecI16:
      case OpCode::IncU8:
      case OpCode::DecU8:
      case OpCode::IncU16:
      case OpCode::DecU16:
      case OpCode::NegI8:
      case OpCode::NegI16:
      case OpCode::NegU8:
      case OpCode::NegU16: {
        if (skipping_unreachable) break;
        if (stack.empty()) {
          reason = "LLVM JIT narrow integer unary underflow";
          return false;
        }
        llvm::Value* raw = stack.back();
        stack.pop_back();
        llvm::Value* value = nullptr;
        if (op == OpCode::IncI8 || op == OpCode::DecI8 || op == OpCode::NegI8) {
          llvm::Value* v = to_i8(raw);
          if (op == OpCode::IncI8) v = builder.CreateAdd(v, builder.getInt8(1));
          if (op == OpCode::DecI8) v = builder.CreateSub(v, builder.getInt8(1));
          if (op == OpCode::NegI8) v = builder.CreateNeg(v);
          value = sext_i8_to_i32(v);
        } else if (op == OpCode::IncI16 || op == OpCode::DecI16 || op == OpCode::NegI16) {
          llvm::Value* v = to_i16(raw);
          if (op == OpCode::IncI16) v = builder.CreateAdd(v, builder.getInt16(1));
          if (op == OpCode::DecI16) v = builder.CreateSub(v, builder.getInt16(1));
          if (op == OpCode::NegI16) v = builder.CreateNeg(v);
          value = sext_i16_to_i32(v);
        } else if (op == OpCode::IncU8 || op == OpCode::DecU8 || op == OpCode::NegU8) {
          llvm::Value* v = to_i8(raw);
          if (op == OpCode::IncU8) v = builder.CreateAdd(v, builder.getInt8(1));
          if (op == OpCode::DecU8) v = builder.CreateSub(v, builder.getInt8(1));
          if (op == OpCode::NegU8) v = builder.CreateNeg(v);
          value = zext_i8_to_i32(v);
        } else {
          llvm::Value* v = to_i16(raw);
          if (op == OpCode::IncU16) v = builder.CreateAdd(v, builder.getInt16(1));
          if (op == OpCode::DecU16) v = builder.CreateSub(v, builder.getInt16(1));
          if (op == OpCode::NegU16) v = builder.CreateNeg(v);
          value = zext_i16_to_i32(v);
        }
        stack.push_back(value);
        break;
      }
      case OpCode::NegI32:
      case OpCode::IncI32:
      case OpCode::DecI32:
      case OpCode::NegU32:
      case OpCode::IncU32:
      case OpCode::DecU32: {
        if (skipping_unreachable) break;
        if (stack.empty()) {
          reason = "LLVM JIT I32 unary underflow";
          return false;
        }
        llvm::Value* value = to_i32(stack.back());
        stack.pop_back();
        if (op == OpCode::NegI32 || op == OpCode::NegU32) value = builder.CreateNeg(value);
        if (op == OpCode::IncI32 || op == OpCode::IncU32) value = builder.CreateAdd(value, builder.getInt32(1));
        if (op == OpCode::DecI32 || op == OpCode::DecU32) value = builder.CreateSub(value, builder.getInt32(1));
        stack.push_back(value);
        break;
      }
      case OpCode::AddI64:
      case OpCode::SubI64:
      case OpCode::MulI64:
      case OpCode::DivI64:
      case OpCode::ModI64:
      case OpCode::AndI64:
      case OpCode::OrI64:
      case OpCode::XorI64:
      case OpCode::ShlI64:
      case OpCode::ShrI64:
      case OpCode::AddU64:
      case OpCode::SubU64:
      case OpCode::MulU64:
      case OpCode::DivU64:
      case OpCode::ModU64: {
        if (skipping_unreachable) break;
        if (stack.size() < 2) {
          reason = "LLVM JIT I64 stack underflow";
          return false;
        }
        llvm::Value* rhs = to_slot(stack.back());
        stack.pop_back();
        llvm::Value* lhs = to_slot(stack.back());
        stack.pop_back();
        llvm::Value* value = nullptr;
        if (op == OpCode::AddI64 || op == OpCode::AddU64) value = builder.CreateAdd(lhs, rhs);
        if (op == OpCode::SubI64 || op == OpCode::SubU64) value = builder.CreateSub(lhs, rhs);
        if (op == OpCode::MulI64 || op == OpCode::MulU64) value = builder.CreateMul(lhs, rhs);
        if (op == OpCode::DivI64 || op == OpCode::ModI64 || op == OpCode::DivU64 || op == OpCode::ModU64) {
          llvm::Value* is_zero = builder.CreateICmpEQ(rhs, builder.getInt64(0));
          llvm::Value* safe_rhs = builder.CreateSelect(is_zero, builder.getInt64(1), rhs);
          if (op == OpCode::DivI64) value = builder.CreateSDiv(lhs, safe_rhs);
          if (op == OpCode::ModI64) value = builder.CreateSRem(lhs, safe_rhs);
          if (op == OpCode::DivU64) value = builder.CreateUDiv(lhs, safe_rhs);
          if (op == OpCode::ModU64) value = builder.CreateURem(lhs, safe_rhs);
          value = builder.CreateSelect(is_zero, builder.getInt64(0), value);
        }
        if (op == OpCode::AndI64) value = builder.CreateAnd(lhs, rhs);
        if (op == OpCode::OrI64) value = builder.CreateOr(lhs, rhs);
        if (op == OpCode::XorI64) value = builder.CreateXor(lhs, rhs);
        if (op == OpCode::ShlI64) value = builder.CreateShl(lhs, builder.CreateAnd(rhs, builder.getInt64(63)));
        if (op == OpCode::ShrI64) value = builder.CreateLShr(lhs, builder.CreateAnd(rhs, builder.getInt64(63)));
        stack.push_back(value);
        break;
      }
      case OpCode::NegI64:
      case OpCode::IncI64:
      case OpCode::DecI64:
      case OpCode::NegU64:
      case OpCode::IncU64:
      case OpCode::DecU64: {
        if (skipping_unreachable) break;
        if (stack.empty()) {
          reason = "LLVM JIT I64 unary underflow";
          return false;
        }
        llvm::Value* value = to_slot(stack.back());
        stack.pop_back();
        if (op == OpCode::NegI64 || op == OpCode::NegU64) value = builder.CreateNeg(value);
        if (op == OpCode::IncI64 || op == OpCode::IncU64) value = builder.CreateAdd(value, builder.getInt64(1));
        if (op == OpCode::DecI64 || op == OpCode::DecU64) value = builder.CreateSub(value, builder.getInt64(1));
        stack.push_back(value);
        break;
      }
      case OpCode::ConvI32ToI64: {
        if (skipping_unreachable) break;
        if (stack.empty()) {
          reason = "LLVM JIT CONV_I32_TO_I64 underflow";
          return false;
        }
        llvm::Value* value = builder.CreateSExt(to_i32(stack.back()), i64);
        stack.pop_back();
        stack.push_back(value);
        break;
      }
      case OpCode::ConvI64ToI32: {
        if (skipping_unreachable) break;
        if (stack.empty()) {
          reason = "LLVM JIT CONV_I64_TO_I32 underflow";
          return false;
        }
        llvm::Value* value = builder.CreateTrunc(to_slot(stack.back()), i32);
        stack.pop_back();
        stack.push_back(value);
        break;
      }
      case OpCode::AddF32:
      case OpCode::SubF32:
      case OpCode::MulF32:
      case OpCode::DivF32: {
        if (skipping_unreachable) break;
        if (stack.size() < 2) { reason = "LLVM JIT F32 stack underflow"; return false; }
        llvm::Value* rhs = to_f32(stack.back()); stack.pop_back();
        llvm::Value* lhs = to_f32(stack.back()); stack.pop_back();
        llvm::Value* value = nullptr;
        if (op == OpCode::AddF32) value = builder.CreateFAdd(lhs, rhs);
        if (op == OpCode::SubF32) value = builder.CreateFSub(lhs, rhs);
        if (op == OpCode::MulF32) value = builder.CreateFMul(lhs, rhs);
        if (op == OpCode::DivF32) value = builder.CreateFDiv(lhs, rhs);
        stack.push_back(f32_to_slot_bits(value));
        break;
      }
      case OpCode::NegF32:
      case OpCode::IncF32:
      case OpCode::DecF32: {
        if (skipping_unreachable) break;
        if (stack.empty()) { reason = "LLVM JIT F32 unary underflow"; return false; }
        llvm::Value* value = to_f32(stack.back()); stack.pop_back();
        if (op == OpCode::NegF32) value = builder.CreateFNeg(value);
        if (op == OpCode::IncF32) value = builder.CreateFAdd(value, llvm::ConstantFP::get(f32, 1.0));
        if (op == OpCode::DecF32) value = builder.CreateFSub(value, llvm::ConstantFP::get(f32, 1.0));
        stack.push_back(f32_to_slot_bits(value));
        break;
      }
      case OpCode::AddF64:
      case OpCode::SubF64:
      case OpCode::MulF64:
      case OpCode::DivF64: {
        if (skipping_unreachable) break;
        if (stack.size() < 2) { reason = "LLVM JIT F64 stack underflow"; return false; }
        llvm::Value* rhs = to_f64(stack.back()); stack.pop_back();
        llvm::Value* lhs = to_f64(stack.back()); stack.pop_back();
        llvm::Value* value = nullptr;
        if (op == OpCode::AddF64) value = builder.CreateFAdd(lhs, rhs);
        if (op == OpCode::SubF64) value = builder.CreateFSub(lhs, rhs);
        if (op == OpCode::MulF64) value = builder.CreateFMul(lhs, rhs);
        if (op == OpCode::DivF64) value = builder.CreateFDiv(lhs, rhs);
        stack.push_back(f64_to_slot_bits(value));
        break;
      }
      case OpCode::NegF64:
      case OpCode::IncF64:
      case OpCode::DecF64: {
        if (skipping_unreachable) break;
        if (stack.empty()) { reason = "LLVM JIT F64 unary underflow"; return false; }
        llvm::Value* value = to_f64(stack.back()); stack.pop_back();
        if (op == OpCode::NegF64) value = builder.CreateFNeg(value);
        if (op == OpCode::IncF64) value = builder.CreateFAdd(value, llvm::ConstantFP::get(f64, 1.0));
        if (op == OpCode::DecF64) value = builder.CreateFSub(value, llvm::ConstantFP::get(f64, 1.0));
        stack.push_back(f64_to_slot_bits(value));
        break;
      }
      case OpCode::ConvI32ToF32: {
        if (skipping_unreachable) break;
        if (stack.empty()) { reason = "LLVM JIT CONV_I32_TO_F32 underflow"; return false; }
        llvm::Value* value = builder.CreateSIToFP(to_i32(stack.back()), f32);
        stack.pop_back(); stack.push_back(f32_to_slot_bits(value));
        break;
      }
      case OpCode::ConvI32ToF64: {
        if (skipping_unreachable) break;
        if (stack.empty()) { reason = "LLVM JIT CONV_I32_TO_F64 underflow"; return false; }
        llvm::Value* value = builder.CreateSIToFP(to_i32(stack.back()), f64);
        stack.pop_back(); stack.push_back(f64_to_slot_bits(value));
        break;
      }
      case OpCode::ConvF32ToI32: {
        if (skipping_unreachable) break;
        if (stack.empty()) { reason = "LLVM JIT CONV_F32_TO_I32 underflow"; return false; }
        llvm::Value* value = builder.CreateFPToSI(to_f32(stack.back()), i32);
        stack.pop_back(); stack.push_back(value);
        break;
      }
      case OpCode::ConvF64ToI32: {
        if (skipping_unreachable) break;
        if (stack.empty()) { reason = "LLVM JIT CONV_F64_TO_I32 underflow"; return false; }
        llvm::Value* value = builder.CreateFPToSI(to_f64(stack.back()), i32);
        stack.pop_back(); stack.push_back(value);
        break;
      }
      case OpCode::ConvF32ToF64: {
        if (skipping_unreachable) break;
        if (stack.empty()) { reason = "LLVM JIT CONV_F32_TO_F64 underflow"; return false; }
        llvm::Value* value = builder.CreateFPExt(to_f32(stack.back()), f64);
        stack.pop_back(); stack.push_back(f64_to_slot_bits(value));
        break;
      }
      case OpCode::ConvF64ToF32: {
        if (skipping_unreachable) break;
        if (stack.empty()) { reason = "LLVM JIT CONV_F64_TO_F32 underflow"; return false; }
        llvm::Value* value = builder.CreateFPTrunc(to_f64(stack.back()), f32);
        stack.pop_back(); stack.push_back(f32_to_slot_bits(value));
        break;
      }
      case OpCode::CmpEqI32:
      case OpCode::CmpNeI32:
      case OpCode::CmpLtI32:
      case OpCode::CmpLeI32:
      case OpCode::CmpGtI32:
      case OpCode::CmpGeI32: {
        if (skipping_unreachable) break;
        if (stack.size() < 2) {
          reason = "LLVM JIT compare stack underflow";
          return false;
        }
        llvm::Value* rhs = to_i32(stack.back());
        stack.pop_back();
        llvm::Value* lhs = to_i32(stack.back());
        stack.pop_back();
        llvm::CmpInst::Predicate pred = llvm::CmpInst::ICMP_EQ;
        if (op == OpCode::CmpEqI32) pred = llvm::CmpInst::ICMP_EQ;
        if (op == OpCode::CmpNeI32) pred = llvm::CmpInst::ICMP_NE;
        if (op == OpCode::CmpLtI32) pred = llvm::CmpInst::ICMP_SLT;
        if (op == OpCode::CmpLeI32) pred = llvm::CmpInst::ICMP_SLE;
        if (op == OpCode::CmpGtI32) pred = llvm::CmpInst::ICMP_SGT;
        if (op == OpCode::CmpGeI32) pred = llvm::CmpInst::ICMP_SGE;
        llvm::Value* cmp = builder.CreateICmp(pred, lhs, rhs);
        stack.push_back(builder.CreateZExt(cmp, i32));
        break;
      }
      case OpCode::CmpEqU32:
      case OpCode::CmpNeU32:
      case OpCode::CmpLtU32:
      case OpCode::CmpLeU32:
      case OpCode::CmpGtU32:
      case OpCode::CmpGeU32: {
        if (skipping_unreachable) break;
        if (stack.size() < 2) {
          reason = "LLVM JIT U32 compare stack underflow";
          return false;
        }
        llvm::Value* rhs = to_i32(stack.back());
        stack.pop_back();
        llvm::Value* lhs = to_i32(stack.back());
        stack.pop_back();
        llvm::CmpInst::Predicate pred = llvm::CmpInst::ICMP_EQ;
        if (op == OpCode::CmpEqU32) pred = llvm::CmpInst::ICMP_EQ;
        if (op == OpCode::CmpNeU32) pred = llvm::CmpInst::ICMP_NE;
        if (op == OpCode::CmpLtU32) pred = llvm::CmpInst::ICMP_ULT;
        if (op == OpCode::CmpLeU32) pred = llvm::CmpInst::ICMP_ULE;
        if (op == OpCode::CmpGtU32) pred = llvm::CmpInst::ICMP_UGT;
        if (op == OpCode::CmpGeU32) pred = llvm::CmpInst::ICMP_UGE;
        llvm::Value* cmp = builder.CreateICmp(pred, lhs, rhs);
        stack.push_back(builder.CreateZExt(cmp, i32));
        break;
      }
      case OpCode::CmpEqI64:
      case OpCode::CmpNeI64:
      case OpCode::CmpLtI64:
      case OpCode::CmpLeI64:
      case OpCode::CmpGtI64:
      case OpCode::CmpGeI64: {
        if (skipping_unreachable) break;
        if (stack.size() < 2) {
          reason = "LLVM JIT I64 compare stack underflow";
          return false;
        }
        llvm::Value* rhs = to_slot(stack.back());
        stack.pop_back();
        llvm::Value* lhs = to_slot(stack.back());
        stack.pop_back();
        llvm::CmpInst::Predicate pred = llvm::CmpInst::ICMP_EQ;
        if (op == OpCode::CmpEqI64) pred = llvm::CmpInst::ICMP_EQ;
        if (op == OpCode::CmpNeI64) pred = llvm::CmpInst::ICMP_NE;
        if (op == OpCode::CmpLtI64) pred = llvm::CmpInst::ICMP_SLT;
        if (op == OpCode::CmpLeI64) pred = llvm::CmpInst::ICMP_SLE;
        if (op == OpCode::CmpGtI64) pred = llvm::CmpInst::ICMP_SGT;
        if (op == OpCode::CmpGeI64) pred = llvm::CmpInst::ICMP_SGE;
        llvm::Value* cmp = builder.CreateICmp(pred, lhs, rhs);
        stack.push_back(builder.CreateZExt(cmp, i32));
        break;
      }
      case OpCode::CmpEqU64:
      case OpCode::CmpNeU64:
      case OpCode::CmpLtU64:
      case OpCode::CmpLeU64:
      case OpCode::CmpGtU64:
      case OpCode::CmpGeU64: {
        if (skipping_unreachable) break;
        if (stack.size() < 2) {
          reason = "LLVM JIT U64 compare stack underflow";
          return false;
        }
        llvm::Value* rhs = to_slot(stack.back());
        stack.pop_back();
        llvm::Value* lhs = to_slot(stack.back());
        stack.pop_back();
        llvm::CmpInst::Predicate pred = llvm::CmpInst::ICMP_EQ;
        if (op == OpCode::CmpEqU64) pred = llvm::CmpInst::ICMP_EQ;
        if (op == OpCode::CmpNeU64) pred = llvm::CmpInst::ICMP_NE;
        if (op == OpCode::CmpLtU64) pred = llvm::CmpInst::ICMP_ULT;
        if (op == OpCode::CmpLeU64) pred = llvm::CmpInst::ICMP_ULE;
        if (op == OpCode::CmpGtU64) pred = llvm::CmpInst::ICMP_UGT;
        if (op == OpCode::CmpGeU64) pred = llvm::CmpInst::ICMP_UGE;
        llvm::Value* cmp = builder.CreateICmp(pred, lhs, rhs);
        stack.push_back(builder.CreateZExt(cmp, i32));
        break;
      }
      case OpCode::CmpEqF32:
      case OpCode::CmpNeF32:
      case OpCode::CmpLtF32:
      case OpCode::CmpLeF32:
      case OpCode::CmpGtF32:
      case OpCode::CmpGeF32: {
        if (skipping_unreachable) break;
        if (stack.size() < 2) { reason = "LLVM JIT F32 compare stack underflow"; return false; }
        llvm::Value* rhs = to_f32(stack.back()); stack.pop_back();
        llvm::Value* lhs = to_f32(stack.back()); stack.pop_back();
        llvm::CmpInst::Predicate pred = llvm::CmpInst::FCMP_OEQ;
        if (op == OpCode::CmpEqF32) pred = llvm::CmpInst::FCMP_OEQ;
        if (op == OpCode::CmpNeF32) pred = llvm::CmpInst::FCMP_ONE;
        if (op == OpCode::CmpLtF32) pred = llvm::CmpInst::FCMP_OLT;
        if (op == OpCode::CmpLeF32) pred = llvm::CmpInst::FCMP_OLE;
        if (op == OpCode::CmpGtF32) pred = llvm::CmpInst::FCMP_OGT;
        if (op == OpCode::CmpGeF32) pred = llvm::CmpInst::FCMP_OGE;
        stack.push_back(builder.CreateZExt(builder.CreateFCmp(pred, lhs, rhs), i32));
        break;
      }
      case OpCode::CmpEqF64:
      case OpCode::CmpNeF64:
      case OpCode::CmpLtF64:
      case OpCode::CmpLeF64:
      case OpCode::CmpGtF64:
      case OpCode::CmpGeF64: {
        if (skipping_unreachable) break;
        if (stack.size() < 2) { reason = "LLVM JIT F64 compare stack underflow"; return false; }
        llvm::Value* rhs = to_f64(stack.back()); stack.pop_back();
        llvm::Value* lhs = to_f64(stack.back()); stack.pop_back();
        llvm::CmpInst::Predicate pred = llvm::CmpInst::FCMP_OEQ;
        if (op == OpCode::CmpEqF64) pred = llvm::CmpInst::FCMP_OEQ;
        if (op == OpCode::CmpNeF64) pred = llvm::CmpInst::FCMP_ONE;
        if (op == OpCode::CmpLtF64) pred = llvm::CmpInst::FCMP_OLT;
        if (op == OpCode::CmpLeF64) pred = llvm::CmpInst::FCMP_OLE;
        if (op == OpCode::CmpGtF64) pred = llvm::CmpInst::FCMP_OGT;
        if (op == OpCode::CmpGeF64) pred = llvm::CmpInst::FCMP_OGE;
        stack.push_back(builder.CreateZExt(builder.CreateFCmp(pred, lhs, rhs), i32));
        break;
      }
      case OpCode::BoolNot: {
        if (skipping_unreachable) break;
        if (stack.empty()) {
          reason = "LLVM JIT BOOL_NOT underflow";
          return false;
        }
        llvm::Value* v = to_i32(stack.back());
        stack.pop_back();
        llvm::Value* is_zero = builder.CreateICmpEQ(v, builder.getInt32(0));
        stack.push_back(builder.CreateZExt(is_zero, i32));
        break;
      }
      case OpCode::BoolAnd:
      case OpCode::BoolOr: {
        if (skipping_unreachable) break;
        if (stack.size() < 2) {
          reason = "LLVM JIT bool stack underflow";
          return false;
        }
        llvm::Value* rhs = builder.CreateICmpNE(to_i32(stack.back()), builder.getInt32(0));
        stack.pop_back();
        llvm::Value* lhs = builder.CreateICmpNE(to_i32(stack.back()), builder.getInt32(0));
        stack.pop_back();
        llvm::Value* value = (op == OpCode::BoolAnd) ? builder.CreateAnd(lhs, rhs)
                                                     : builder.CreateOr(lhs, rhs);
        stack.push_back(builder.CreateZExt(value, i32));
        break;
      }
      case OpCode::CheckedBounds: {
        if (skipping_unreachable) break;
        if (stack.size() < 3) {
          reason = "LLVM JIT CHECKED_BOUNDS underflow";
          return false;
        }
        llvm::Value* length = to_i32(stack.back());
        stack.pop_back();
        llvm::Value* index = to_i32(stack.back());
        stack.pop_back();
        llvm::Value* value = stack.back();
        stack.pop_back();
        llvm::Value* index_neg = builder.CreateICmpSLT(index, builder.getInt32(0));
        llvm::Value* length_neg = builder.CreateICmpSLT(length, builder.getInt32(0));
        llvm::Value* index_oob = builder.CreateICmpSGE(index, length);
        emit_trap_if(builder.CreateOr(builder.CreateOr(index_neg, length_neg), index_oob), "checked.bounds");
        stack.push_back(value);
        break;
      }
      case OpCode::IsNull: {
        if (skipping_unreachable) break;
        if (stack.empty()) {
          reason = "LLVM JIT IS_NULL underflow";
          return false;
        }
        llvm::Value* value = builder.CreateICmpEQ(to_i32(stack.back()), builder.getInt32(Simple::VM::HeapLayout::kNullRef));
        stack.pop_back();
        stack.push_back(builder.CreateZExt(value, i32));
        break;
      }
      case OpCode::CheckedNull: {
        if (skipping_unreachable) break;
        if (stack.empty()) { reason = "LLVM JIT CHECKED_NULL underflow"; return false; }
        llvm::Value* ref = to_slot(stack.back()); stack.pop_back();
        stack.push_back(builder.CreateCall(checked_ref_helper, {ref}));
        break;
      }
      case OpCode::DropObject: {
        if (skipping_unreachable) break;
        if (stack.empty()) { reason = "LLVM JIT DROP_OBJECT underflow"; return false; }
        llvm::Value* ref = to_slot(stack.back()); stack.pop_back();
        builder.CreateCall(checked_ref_helper, {ref});
        break;
      }
      case OpCode::CloneObject: {
        if (skipping_unreachable) break;
        if (stack.empty()) { reason = "LLVM JIT CLONE_OBJECT underflow"; return false; }
        llvm::Value* ref = to_slot(stack.back()); stack.pop_back();
        stack.push_back(builder.CreateCall(clone_object_helper, {ref}));
        break;
      }
      case OpCode::ObjectEq: {
        if (skipping_unreachable) break;
        if (stack.size() < 2) { reason = "LLVM JIT OBJECT_EQ underflow"; return false; }
        llvm::Value* rhs = to_slot(stack.back()); stack.pop_back();
        llvm::Value* lhs = to_slot(stack.back()); stack.pop_back();
        stack.push_back(builder.CreateCall(object_eq_helper, {lhs, rhs}));
        break;
      }
      case OpCode::StringLen: {
        if (skipping_unreachable) break;
        if (stack.empty()) { reason = "LLVM JIT STRING_LEN underflow"; return false; }
        llvm::Value* ref = to_slot(stack.back()); stack.pop_back();
        stack.push_back(builder.CreateCall(string_len_helper, {ref}));
        break;
      }
      case OpCode::StringConcat: {
        if (skipping_unreachable) break;
        if (stack.size() < 2) { reason = "LLVM JIT STRING_CONCAT underflow"; return false; }
        llvm::Value* rhs = to_slot(stack.back()); stack.pop_back();
        llvm::Value* lhs = to_slot(stack.back()); stack.pop_back();
        stack.push_back(builder.CreateCall(string_concat_helper, {lhs, rhs}));
        break;
      }
      case OpCode::StringEq:
      case OpCode::StringNe:
      case OpCode::StringCompare:
      case OpCode::StringFind: {
        if (skipping_unreachable) break;
        if (stack.size() < 2) { reason = "LLVM JIT string compare underflow"; return false; }
        llvm::Value* rhs = to_slot(stack.back()); stack.pop_back();
        llvm::Value* lhs = to_slot(stack.back()); stack.pop_back();
        uint32_t cmp_op = (op == OpCode::StringEq) ? 0u : (op == OpCode::StringNe) ? 1u : (op == OpCode::StringCompare) ? 2u : 3u;
        stack.push_back(builder.CreateCall(string_compare_helper, {lhs, rhs, builder.getInt32(cmp_op)}));
        break;
      }
      case OpCode::StringGetChar: {
        if (skipping_unreachable) break;
        if (stack.size() < 2) { reason = "LLVM JIT STRING_GET_CHAR underflow"; return false; }
        llvm::Value* index = to_slot(stack.back()); stack.pop_back();
        llvm::Value* ref = to_slot(stack.back()); stack.pop_back();
        stack.push_back(builder.CreateCall(string_get_char_helper, {ref, index}));
        break;
      }
      case OpCode::StringSlice: {
        if (skipping_unreachable) break;
        if (stack.size() < 3) { reason = "LLVM JIT STRING_SLICE underflow"; return false; }
        llvm::Value* end = to_slot(stack.back()); stack.pop_back();
        llvm::Value* start = to_slot(stack.back()); stack.pop_back();
        llvm::Value* ref = to_slot(stack.back()); stack.pop_back();
        stack.push_back(builder.CreateCall(string_slice_helper, {ref, start, end}));
        break;
      }
      case OpCode::RefEq:
      case OpCode::RefNe: {
        if (skipping_unreachable) break;
        if (stack.size() < 2) {
          reason = "LLVM JIT ref compare stack underflow";
          return false;
        }
        llvm::Value* rhs = to_i32(stack.back());
        stack.pop_back();
        llvm::Value* lhs = to_i32(stack.back());
        stack.pop_back();
        llvm::Value* value = builder.CreateICmpEQ(lhs, rhs);
        if (op == OpCode::RefNe) value = builder.CreateNot(value);
        stack.push_back(builder.CreateZExt(value, i32));
        break;
      }
      case OpCode::Pop:
      case OpCode::KeepAlive:
        if (skipping_unreachable) break;
        if (stack.empty()) {
          reason = (op == OpCode::Pop) ? "LLVM JIT POP underflow" : "LLVM JIT KEEP_ALIVE underflow";
          return false;
        }
        stack.pop_back();
        break;
      case OpCode::LoadLocal: {
        uint32_t idx = Simple::VM::Interpreter::ReadU32(module.code, pc);
        if (skipping_unreachable) break;
        if (idx >= locals.size()) {
          reason = "LLVM JIT LOAD_LOCAL invalid index";
          return false;
        }
        llvm::Value* loaded = builder.CreateLoad(i64, locals[idx]);
        if (idx < local_type_ids.size()) note_value_type(loaded, local_type_ids[idx]);
        stack.push_back(loaded);
        break;
      }
      case OpCode::StoreLocal: {
        uint32_t idx = Simple::VM::Interpreter::ReadU32(module.code, pc);
        if (skipping_unreachable) break;
        if (idx >= locals.size()) {
          reason = "LLVM JIT STORE_LOCAL invalid index";
          return false;
        }
        if (stack.empty()) {
          reason = "LLVM JIT STORE_LOCAL underflow";
          return false;
        }
        llvm::Value* source = stack.back();
        llvm::Value* stored = to_slot(source);
        if (idx < local_type_ids.size()) {
          local_type_ids[idx] = value_type_id(source);
          note_value_type(stored, local_type_ids[idx]);
        }
        builder.CreateStore(stored, locals[idx]);
        stack.pop_back();
        break;
      }
      case OpCode::LoadGlobal: {
        uint32_t idx = Simple::VM::Interpreter::ReadU32(module.code, pc);
        if (skipping_unreachable) break;
        if (globals_ptr) {
          if (idx >= module.globals.size()) {
            reason = "LLVM JIT LOAD_GLOBAL invalid index";
            return false;
          }
          llvm::Value* loaded = builder.CreateCall(load_global_helper, {builder.getInt32(idx)});
          note_value_type(loaded, module.globals[idx].type_id);
          stack.push_back(loaded);
          break;
        }
        if (idx >= globals.size()) {
          reason = "LLVM JIT LOAD_GLOBAL invalid index";
          return false;
        }
        llvm::Value* loaded = builder.CreateLoad(i64, globals[idx]);
        note_value_type(loaded, module.globals[idx].type_id);
        stack.push_back(loaded);
        break;
      }
      case OpCode::StoreGlobal: {
        uint32_t idx = Simple::VM::Interpreter::ReadU32(module.code, pc);
        if (skipping_unreachable) break;
        if (globals_ptr) {
          if (idx >= module.globals.size()) {
            reason = "LLVM JIT STORE_GLOBAL invalid index";
            return false;
          }
          if (stack.empty()) {
            reason = "LLVM JIT STORE_GLOBAL underflow";
            return false;
          }
          builder.CreateCall(store_global_helper, {builder.getInt32(idx), to_slot(stack.back())});
          stack.pop_back();
          break;
        }
        if (idx >= globals.size()) {
          reason = "LLVM JIT STORE_GLOBAL invalid index";
          return false;
        }
        if (stack.empty()) {
          reason = "LLVM JIT STORE_GLOBAL underflow";
          return false;
        }
        builder.CreateStore(to_slot(stack.back()), globals[idx]);
        stack.pop_back();
        break;
      }
      case OpCode::NewClosure: {
        uint32_t method_id = ReadU32(module.code, pc);
        uint8_t upvalue_count = ReadU8(module.code, pc);
        if (skipping_unreachable) break;
        if (method_id >= module.methods.size()) { reason = "LLVM JIT NEW_CLOSURE bad method id"; return false; }
        if (upvalue_count != 0) { reason = "unsupported: NEW_CLOSURE with upvalues needs closure runtime ABI"; return false; }
        int32_t target_func = -1;
        for (size_t i = 0; i < module.functions.size(); ++i) {
          if (module.functions[i].method_id == method_id) { target_func = static_cast<int32_t>(i); break; }
        }
        if (target_func < 0) { reason = "LLVM JIT NEW_CLOSURE method not found"; return false; }
        stack.push_back(builder.getInt32(target_func));
        break;
      }
      case OpCode::NewObject: {
        uint32_t type_id = ReadU32(module.code, pc);
        if (skipping_unreachable) break;
        if (type_id >= module.types.size()) { reason = "LLVM JIT NEW_OBJECT bad type id"; return false; }
        stack.push_back(builder.CreateCall(new_object_helper,
                                           {builder.getInt32(type_id), builder.getInt32(module.types[type_id].size)}));
        break;
      }
      case OpCode::LoadField: {
        uint32_t field_id = ReadU32(module.code, pc);
        if (skipping_unreachable) break;
        if (field_id >= module.fields.size()) { reason = "LLVM JIT LOAD_FIELD bad field id"; return false; }
        if (stack.empty()) { reason = "LLVM JIT LOAD_FIELD underflow"; return false; }
        llvm::Value* ref = to_slot(stack.back()); stack.pop_back();
        stack.push_back(builder.CreateCall(load_field_helper,
                                           {ref, builder.getInt32(module.fields[field_id].offset)}));
        break;
      }
      case OpCode::StoreField: {
        uint32_t field_id = ReadU32(module.code, pc);
        if (skipping_unreachable) break;
        if (field_id >= module.fields.size()) { reason = "LLVM JIT STORE_FIELD bad field id"; return false; }
        if (stack.size() < 2) { reason = "LLVM JIT STORE_FIELD underflow"; return false; }
        llvm::Value* value = to_slot(stack.back()); stack.pop_back();
        llvm::Value* ref = to_slot(stack.back()); stack.pop_back();
        builder.CreateCall(store_field_helper, {ref, builder.getInt32(module.fields[field_id].offset), value});
        break;
      }
      case OpCode::NewArray:
      case OpCode::NewArrayI64:
      case OpCode::NewArrayF32:
      case OpCode::NewArrayF64:
      case OpCode::NewArrayRef:
      case OpCode::NewList:
      case OpCode::NewListI64:
      case OpCode::NewListF32:
      case OpCode::NewListF64:
      case OpCode::NewListRef: {
        uint32_t type_id = ReadU32(module.code, pc);
        uint32_t length = ReadU32(module.code, pc);
        if (skipping_unreachable) break;
        uint32_t element_size = 4;
        if (op == OpCode::NewArrayI64 || op == OpCode::NewArrayF64 ||
            op == OpCode::NewListI64 || op == OpCode::NewListF64) element_size = 8;
        bool is_list = op == OpCode::NewList || op == OpCode::NewListI64 ||
                       op == OpCode::NewListF32 || op == OpCode::NewListF64 ||
                       op == OpCode::NewListRef;
        stack.push_back(builder.CreateCall(is_list ? new_list_helper : new_array_helper,
                                           {builder.getInt32(type_id), builder.getInt32(length),
                                            builder.getInt32(element_size)}));
        break;
      }
      case OpCode::TypeOf: {
        if (skipping_unreachable) break;
        if (stack.empty()) { reason = "LLVM JIT TYPEOF underflow"; return false; }
        llvm::Value* ref = to_slot(stack.back()); stack.pop_back();
        stack.push_back(builder.CreateCall(type_of_helper, {ref}));
        break;
      }
      case OpCode::ArrayLen: {
        if (skipping_unreachable) break;
        if (stack.empty()) { reason = "LLVM JIT ARRAY_LEN underflow"; return false; }
        llvm::Value* ref = to_slot(stack.back());
        stack.pop_back();
        stack.push_back(builder.CreateCall(array_len_helper, {ref}));
        break;
      }
      case OpCode::ArrayGetI32:
      case OpCode::ArrayGetI64:
      case OpCode::ArrayGetF32:
      case OpCode::ArrayGetF64:
      case OpCode::ArrayGetRef: {
        if (skipping_unreachable) break;
        if (stack.size() < 2) { reason = "LLVM JIT ARRAY_GET underflow"; return false; }
        llvm::FunctionCallee helper = array_get_f32_helper;
        if (op == OpCode::ArrayGetI32) helper = array_get_i32_helper;
        else if (op == OpCode::ArrayGetI64) helper = array_get_i64_helper;
        else if (op == OpCode::ArrayGetF64) helper = array_get_f64_helper;
        else if (op == OpCode::ArrayGetRef) helper = array_get_ref_helper;
        llvm::Value* idx = to_slot(stack.back()); stack.pop_back();
        llvm::Value* ref = to_slot(stack.back()); stack.pop_back();
        stack.push_back(builder.CreateCall(helper, {ref, idx}));
        break;
      }
      case OpCode::ArraySetI32:
      case OpCode::ArraySetI64:
      case OpCode::ArraySetF32:
      case OpCode::ArraySetF64:
      case OpCode::ArraySetRef: {
        if (skipping_unreachable) break;
        if (stack.size() < 3) { reason = "LLVM JIT ARRAY_SET underflow"; return false; }
        llvm::FunctionCallee helper = array_set_f32_helper;
        if (op == OpCode::ArraySetI32) helper = array_set_i32_helper;
        else if (op == OpCode::ArraySetI64) helper = array_set_i64_helper;
        else if (op == OpCode::ArraySetF64) helper = array_set_f64_helper;
        else if (op == OpCode::ArraySetRef) helper = array_set_ref_helper;
        llvm::Value* value = to_slot(stack.back()); stack.pop_back();
        llvm::Value* idx = to_slot(stack.back()); stack.pop_back();
        llvm::Value* ref = to_slot(stack.back()); stack.pop_back();
        builder.CreateCall(helper, {ref, idx, value});
        break;
      }
      case OpCode::ListLen: {
        if (skipping_unreachable) break;
        if (stack.empty()) { reason = "LLVM JIT LIST_LEN underflow"; return false; }
        llvm::Value* ref = to_slot(stack.back()); stack.pop_back();
        stack.push_back(builder.CreateCall(list_len_helper, {ref}));
        break;
      }
      case OpCode::ListGetI32:
      case OpCode::ListGetF32:
      case OpCode::ListGetRef:
      case OpCode::ListGetI64:
      case OpCode::ListGetF64: {
        if (skipping_unreachable) break;
        if (stack.size() < 2) { reason = "LLVM JIT LIST_GET underflow"; return false; }
        llvm::FunctionCallee helper = (op == OpCode::ListGetI64 || op == OpCode::ListGetF64) ? list_get64_helper : list_get32_helper;
        llvm::Value* idx = to_slot(stack.back()); stack.pop_back();
        llvm::Value* ref = to_slot(stack.back()); stack.pop_back();
        stack.push_back(builder.CreateCall(helper, {ref, idx}));
        break;
      }
      case OpCode::ListSetI32:
      case OpCode::ListSetF32:
      case OpCode::ListSetRef:
      case OpCode::ListSetI64:
      case OpCode::ListSetF64: {
        if (skipping_unreachable) break;
        if (stack.size() < 3) { reason = "LLVM JIT LIST_SET underflow"; return false; }
        llvm::FunctionCallee helper = (op == OpCode::ListSetI64 || op == OpCode::ListSetF64) ? list_set64_helper : list_set32_helper;
        llvm::Value* value = to_slot(stack.back()); stack.pop_back();
        llvm::Value* idx = to_slot(stack.back()); stack.pop_back();
        llvm::Value* ref = to_slot(stack.back()); stack.pop_back();
        builder.CreateCall(helper, {ref, idx, value});
        break;
      }
      case OpCode::ListPushI32:
      case OpCode::ListPushF32:
      case OpCode::ListPushRef:
      case OpCode::ListPushI64:
      case OpCode::ListPushF64: {
        if (skipping_unreachable) break;
        if (stack.size() < 2) { reason = "LLVM JIT LIST_PUSH underflow"; return false; }
        llvm::FunctionCallee helper = (op == OpCode::ListPushI64 || op == OpCode::ListPushF64) ? list_push64_helper : list_push32_helper;
        llvm::Value* value = to_slot(stack.back()); stack.pop_back();
        llvm::Value* ref = to_slot(stack.back()); stack.pop_back();
        builder.CreateCall(helper, {ref, value});
        break;
      }
      case OpCode::ListPopI32:
      case OpCode::ListPopF32:
      case OpCode::ListPopRef:
      case OpCode::ListPopI64:
      case OpCode::ListPopF64: {
        if (skipping_unreachable) break;
        if (stack.empty()) { reason = "LLVM JIT LIST_POP underflow"; return false; }
        llvm::FunctionCallee helper = (op == OpCode::ListPopI64 || op == OpCode::ListPopF64) ? list_pop64_helper : list_pop32_helper;
        llvm::Value* ref = to_slot(stack.back()); stack.pop_back();
        stack.push_back(builder.CreateCall(helper, {ref}));
        break;
      }
      case OpCode::ListInsertI32:
      case OpCode::ListInsertF32:
      case OpCode::ListInsertRef:
      case OpCode::ListInsertI64:
      case OpCode::ListInsertF64: {
        if (skipping_unreachable) break;
        if (stack.size() < 3) { reason = "LLVM JIT LIST_INSERT underflow"; return false; }
        llvm::FunctionCallee helper = (op == OpCode::ListInsertI64 || op == OpCode::ListInsertF64) ? list_insert64_helper : list_insert32_helper;
        llvm::Value* value = to_slot(stack.back()); stack.pop_back();
        llvm::Value* idx = to_slot(stack.back()); stack.pop_back();
        llvm::Value* ref = to_slot(stack.back()); stack.pop_back();
        builder.CreateCall(helper, {ref, idx, value});
        break;
      }
      case OpCode::ListRemoveI32:
      case OpCode::ListRemoveF32:
      case OpCode::ListRemoveRef:
      case OpCode::ListRemoveI64:
      case OpCode::ListRemoveF64: {
        if (skipping_unreachable) break;
        if (stack.size() < 2) { reason = "LLVM JIT LIST_REMOVE underflow"; return false; }
        llvm::FunctionCallee helper = (op == OpCode::ListRemoveI64 || op == OpCode::ListRemoveF64) ? list_remove64_helper : list_remove32_helper;
        llvm::Value* idx = to_slot(stack.back()); stack.pop_back();
        llvm::Value* ref = to_slot(stack.back()); stack.pop_back();
        stack.push_back(builder.CreateCall(helper, {ref, idx}));
        break;
      }
      case OpCode::ListClear: {
        if (skipping_unreachable) break;
        if (stack.empty()) { reason = "LLVM JIT LIST_CLEAR underflow"; return false; }
        llvm::Value* ref = to_slot(stack.back()); stack.pop_back();
        builder.CreateCall(list_clear_helper, {ref});
        break;
      }
      case OpCode::ListReserve: {
        if (skipping_unreachable) break;
        if (stack.size() < 2) { reason = "LLVM JIT LIST_RESERVE underflow"; return false; }
        llvm::Value* capacity = to_slot(stack.back()); stack.pop_back();
        llvm::Value* ref = to_slot(stack.back()); stack.pop_back();
        builder.CreateCall(list_reserve_helper, {ref, capacity});
        break;
      }
      case OpCode::ListResize: {
        if (skipping_unreachable) break;
        if (stack.size() < 3) { reason = "LLVM JIT LIST_RESIZE underflow"; return false; }
        llvm::Value* fill = to_slot(stack.back()); stack.pop_back();
        llvm::Value* size = to_slot(stack.back()); stack.pop_back();
        llvm::Value* ref = to_slot(stack.back()); stack.pop_back();
        builder.CreateCall(list_resize_helper, {ref, size, fill});
        break;
      }
      case OpCode::JmpTable: {
        uint32_t const_id = ReadU32(module.code, pc);
        int32_t default_rel = ReadI32(module.code, pc);
        if (skipping_unreachable) break;
        if (stack.empty()) {
          reason = "LLVM JIT JMP_TABLE underflow";
          return false;
        }
        JmpTableInfo table;
        if (!parse_jmp_table(const_id, table, reason)) return false;
        llvm::Value* index = to_i32(stack.back());
        stack.pop_back();
        size_t default_target = static_cast<size_t>(static_cast<int64_t>(pc) + default_rel);
        llvm::BasicBlock* pred_block = builder.GetInsertBlock();
        if (!merge_block_stack(default_target, stack, pred_block)) return false;
        llvm::SwitchInst* sw = builder.CreateSwitch(index, get_block(default_target), table.count);
        for (uint32_t i = 0; i < table.count; ++i) {
          size_t target = static_cast<size_t>(static_cast<int64_t>(pc) + table.rels[i]);
          if (!merge_block_stack(target, stack, pred_block)) return false;
          sw->addCase(builder.getInt32(i), get_block(target));
        }
        break;
      }
      case OpCode::Jmp: {
        int32_t rel = ReadI32(module.code, pc);
        if (skipping_unreachable) break;
        size_t target = static_cast<size_t>(static_cast<int64_t>(pc) + rel);
        if (!merge_block_stack(target, stack, builder.GetInsertBlock())) return false;
        builder.CreateBr(get_block(target));
        break;
      }
      case OpCode::SysCall:
        ReadU32(module.code, pc);
        if (skipping_unreachable) break;
        builder.CreateCall(trap_helper, {});
        builder.CreateRet(builder.getInt64(0));
        stack.clear();
        break;
      case OpCode::CallIndirect: {
        uint32_t sig_id = ReadU32(module.code, pc);
        uint8_t arg_count = ReadU8(module.code, pc);
        if (skipping_unreachable) break;
        if (sig_id >= module.sigs.size()) {
          reason = "LLVM JIT CALL_INDIRECT invalid signature id";
          return false;
        }
        const auto& target_sig = module.sigs[sig_id];
        if (arg_count != target_sig.param_count) {
          reason = "LLVM JIT CALL_INDIRECT arg count mismatch";
          return false;
        }
        if (stack.size() < static_cast<size_t>(arg_count) + 1u) {
          reason = "LLVM JIT CALL_INDIRECT stack underflow";
          return false;
        }
        llvm::Value* target_func = to_i32(stack.back());
        stack.pop_back();
        llvm::AllocaInst* call_args = create_entry_alloca(i64, builder.getInt32(arg_count), "call_indirect_args");
        for (int i = static_cast<int>(arg_count) - 1; i >= 0; --i) {
          llvm::Value* arg = to_slot(stack.back());
          stack.pop_back();
          llvm::Value* ptr = builder.CreateGEP(i64, call_args, builder.getInt64(static_cast<uint64_t>(i)));
          builder.CreateStore(arg, ptr);
        }
        llvm::AllocaInst* has_ret_ptr = create_entry_alloca(builder.getInt8Ty(), nullptr, "call_indirect_has_ret");
        builder.CreateStore(builder.getInt8(0), has_ret_ptr);
        llvm::Value* result = emit_call_helper(target_func, call_args, arg_count, has_ret_ptr, instr_pc,
                                               true, true);
        if (!sig_returns_void(target_sig)) {
          note_value_type(result, target_sig.ret_type_id);
          stack.push_back(result);
        }
        break;
      }
      case OpCode::CallImport:
      case OpCode::Call:
      case OpCode::TailCall: {
        uint32_t target_func = ReadU32(module.code, pc);
        uint8_t arg_count = ReadU8(module.code, pc);
        if (skipping_unreachable) break;
        if (target_func >= module.functions.size()) {
          reason = "LLVM JIT CALL invalid function id";
          return false;
        }
        const auto& target_function = module.functions[target_func];
        if (target_function.method_id >= module.methods.size()) {
          reason = "LLVM JIT CALL invalid method id";
          return false;
        }
        const auto& target_method = module.methods[target_function.method_id];
        if (target_method.sig_id >= module.sigs.size()) {
          reason = "LLVM JIT CALL invalid signature id";
          return false;
        }
        const auto& target_sig = module.sigs[target_method.sig_id];
        if (arg_count != target_sig.param_count) {
          reason = "LLVM JIT CALL arg count mismatch";
          return false;
        }
        const bool import_like_call =
            target_func < module.function_is_import.size() && module.function_is_import[target_func];
        const bool dynamic_dl_direct_call = import_like_call && [&]() {
          std::string module_name;
          std::string symbol_name;
          return import_name(target_func, &module_name, &symbol_name) && module_name == "System.dl" &&
                 symbol_name.rfind("call$", 0) == 0 && dl_call_loop_safe(target_sig);
        }();
        if (target_func == func_index && arg_count != param_count) {
          reason = "LLVM JIT self CALL arg count mismatch";
          return false;
        }
        if (stack.size() < arg_count) {
          reason = "LLVM JIT CALL stack underflow";
          return false;
        }
        llvm::AllocaInst* call_args = create_entry_alloca(i64, builder.getInt32(arg_count), "call_args");
        for (int i = static_cast<int>(arg_count) - 1; i >= 0; --i) {
          llvm::Value* arg = to_slot(stack.back());
          stack.pop_back();
          llvm::Value* ptr = builder.CreateGEP(i64, call_args, builder.getInt64(static_cast<uint64_t>(i)));
          builder.CreateStore(arg, ptr);
        }
        llvm::Value* result = nullptr;
        if (target_func == func_index) {
          result = builder.CreateCall(fn_type, fn, {call_args, builder.getInt32(arg_count)});
        } else if (dynamic_dl_direct_call) {
          llvm::AllocaInst* has_ret_ptr = create_entry_alloca(builder.getInt8Ty(), nullptr, "dynamic_dl_has_ret");
          builder.CreateStore(builder.getInt8(0), has_ret_ptr);
          result = emit_dynamic_dl_helper(target_func, call_args, arg_count, has_ret_ptr, instr_pc);
        } else {
          llvm::AllocaInst* has_ret_ptr = create_entry_alloca(builder.getInt8Ty(), nullptr, "call_has_ret");
          builder.CreateStore(builder.getInt8(0), has_ret_ptr);
          const auto [may_block, may_allocate] = helper_call_safepoint_flags(target_func);
          result = emit_call_helper(builder.getInt32(target_func), call_args, arg_count, has_ret_ptr, instr_pc,
                                    may_block, may_allocate);
        }
        const bool returns_void = sig_returns_void(target_sig);
        if (op == OpCode::TailCall) {
          native_has_ret = !returns_void;
          builder.CreateRet(returns_void ? builder.getInt64(0) : result);
          stack.clear();
        } else if (!returns_void) {
          note_value_type(result, target_sig.ret_type_id);
          stack.push_back(result);
        }
        break;
      }
      case OpCode::JmpTrue:
      case OpCode::JmpFalse: {
        int32_t rel = ReadI32(module.code, pc);
        if (skipping_unreachable) break;
        if (stack.empty()) {
          reason = "LLVM JIT conditional jump underflow";
          return false;
        }
        llvm::Value* cond = builder.CreateICmpNE(to_i32(stack.back()), builder.getInt32(0));
        stack.pop_back();
        if (op == OpCode::JmpFalse) cond = builder.CreateNot(cond);
        size_t target = static_cast<size_t>(static_cast<int64_t>(pc) + rel);
        llvm::BasicBlock* pred_block = builder.GetInsertBlock();
        if (!merge_block_stack(target, stack, pred_block)) return false;
        if (!merge_block_stack(pc, stack, pred_block)) return false;
        llvm::BasicBlock* target_block = get_block(target);
        llvm::BasicBlock* fallthrough_block = get_block(pc);
        builder.CreateCondBr(cond, target_block, fallthrough_block);
        builder.SetInsertPoint(fallthrough_block);
        break;
      }
      case OpCode::Halt:
      case OpCode::Ret: {
        if (skipping_unreachable) break;
        if (stack.empty()) {
          native_has_ret = false;
          builder.CreateRet(builder.getInt64(0));
          stack.clear();
        } else {
          native_has_ret = true;
          builder.CreateRet(to_slot(stack.back()));
          stack.clear();
        }
        break;
      }
      default:
        reason = std::string("unsupported opcode: ") + Simple::Byte::OpCodeName(static_cast<uint8_t>(op));
        return false;
    }
  }

  std::string verify_error;
  llvm::raw_string_ostream verify_stream(verify_error);
  if (llvm::verifyModule(*ir_module, &verify_stream)) {
    reason = "unsupported: LLVM JIT generated invalid IR: " + verify_stream.str();
    return false;
  }

  if (llvm::Error err = jit->addIRModule(llvm::orc::ThreadSafeModule(std::move(ir_module), std::move(context)))) {
    reason = ToString(std::move(err));
    return false;
  }
  auto symbol = jit->lookup(symbol_name);
  if (!symbol) {
    reason = ToString(symbol.takeError());
    return false;
  }

  EntryFn entry_fn = reinterpret_cast<EntryFn>(static_cast<uintptr_t>(symbol->getValue()));
  auto cached = std::make_shared<CachedLlvmEntry>();
  cached->entry = entry_fn;
  cached->has_ret = native_has_ret;
  cached->jit = std::move(jit);
  {
    std::lock_guard<std::mutex> lock(LlvmCacheMutex());
    LlvmCache()[cache_key] = cached;
  }
  return RunCachedEntry(cached, module, args, heap, globals_ptr, exec_options, out_ret, out_has_ret, reason);
#endif
}

} // namespace Simple::VM::Jit
