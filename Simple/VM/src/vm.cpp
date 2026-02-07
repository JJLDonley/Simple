#include "vm.h"

#include <chrono>
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <dlfcn.h>
#include <filesystem>
#include <limits>
#include <sstream>
#include <thread>
#include <tuple>
#include <type_traits>
#include <vector>

#include "heap.h"
#include "intrinsic_ids.h"
#include "opcode.h"
#include "scratch_arena.h"
#include "sbc_verifier.h"

namespace Simple::VM {
namespace {

using Simple::Byte::OpCode;
using Simple::Byte::OpCodeName;
using Simple::Byte::TypeKind;
using Slot = uint64_t;
constexpr uint32_t kNullRef = 0xFFFFFFFFu;

inline bool IsI32LikeImportType(TypeKind kind) {
  switch (kind) {
    case TypeKind::I8:
    case TypeKind::I16:
    case TypeKind::I32:
    case TypeKind::U8:
    case TypeKind::U16:
    case TypeKind::U32:
    case TypeKind::Bool:
    case TypeKind::Char:
      return true;
    default:
      return false;
  }
}

inline bool IsI64LikeImportType(TypeKind kind) {
  return kind == TypeKind::I64 || kind == TypeKind::U64;
}

inline bool IsStringLikeImportType(TypeKind kind) {
  return kind == TypeKind::String || kind == TypeKind::Ref;
}

std::u16string AsciiToU16(const std::string& text);
std::string U16ToAscii(const std::u16string& text);
uint32_t CreateString(Heap& heap, const std::u16string& text);
std::u16string ReadString(const HeapObject* obj);

inline bool IsDlCallScalarKind(TypeKind kind, bool allow_void) {
  if (allow_void && kind == TypeKind::Unspecified) return true;
  switch (kind) {
    case TypeKind::I8:
    case TypeKind::I16:
    case TypeKind::I32:
    case TypeKind::I64:
    case TypeKind::U8:
    case TypeKind::U16:
    case TypeKind::U32:
    case TypeKind::U64:
    case TypeKind::F32:
    case TypeKind::F64:
    case TypeKind::Bool:
    case TypeKind::Char:
    case TypeKind::String:
      return true;
    default:
      return false;
  }
}

float BitsToF32(uint32_t bits) {
  uint32_t v = bits;
  float out = 0.0f;
  std::memcpy(&out, &v, sizeof(out));
  return out;
}

double BitsToF64(uint64_t bits) {
  uint64_t v = bits;
  double out = 0.0;
  std::memcpy(&out, &v, sizeof(out));
  return out;
}

uint32_t F32ToBits(float value) {
  uint32_t bits = 0;
  std::memcpy(&bits, &value, sizeof(bits));
  return bits;
}

uint64_t F64ToBits(double value) {
  uint64_t bits = 0;
  std::memcpy(&bits, &value, sizeof(bits));
  return bits;
}

inline Slot PackI32(int32_t value) {
  return static_cast<uint32_t>(value);
}

inline int32_t UnpackI32(Slot value) {
  return static_cast<int32_t>(static_cast<uint32_t>(value));
}

inline Slot PackI64(int64_t value) {
  return static_cast<uint64_t>(value);
}

inline int64_t UnpackI64(Slot value) {
  return static_cast<int64_t>(value);
}

inline uint32_t UnpackU32Bits(Slot value) {
  return static_cast<uint32_t>(value);
}

inline uint64_t UnpackU64Bits(Slot value) {
  return static_cast<uint64_t>(value);
}

inline Slot PackF32Bits(uint32_t bits) {
  return static_cast<uint64_t>(bits);
}

inline Slot PackF64Bits(uint64_t bits) {
  return bits;
}

inline Slot PackRef(uint32_t handle) {
  return static_cast<uint64_t>(handle);
}

inline uint32_t UnpackRef(Slot value) {
  return static_cast<uint32_t>(value);
}

inline bool IsNullRef(Slot value) {
  return UnpackRef(value) == kNullRef;
}

template <typename T>
bool ConvertDlArg(Slot slot,
                  Heap& heap,
                  std::vector<std::string>& owned_strings,
                  T* out,
                  std::string* out_error) {
  if (!out) return false;
  if constexpr (std::is_same_v<T, int8_t>) {
    *out = static_cast<int8_t>(UnpackI32(slot));
    return true;
  } else if constexpr (std::is_same_v<T, int16_t>) {
    *out = static_cast<int16_t>(UnpackI32(slot));
    return true;
  } else if constexpr (std::is_same_v<T, int32_t>) {
    *out = static_cast<int32_t>(UnpackI32(slot));
    return true;
  } else if constexpr (std::is_same_v<T, int64_t>) {
    *out = static_cast<int64_t>(UnpackI64(slot));
    return true;
  } else if constexpr (std::is_same_v<T, uint8_t>) {
    *out = static_cast<uint8_t>(UnpackI32(slot));
    return true;
  } else if constexpr (std::is_same_v<T, uint16_t>) {
    *out = static_cast<uint16_t>(UnpackI32(slot));
    return true;
  } else if constexpr (std::is_same_v<T, uint32_t>) {
    *out = static_cast<uint32_t>(UnpackI32(slot));
    return true;
  } else if constexpr (std::is_same_v<T, uint64_t>) {
    *out = static_cast<uint64_t>(UnpackI64(slot));
    return true;
  } else if constexpr (std::is_same_v<T, float>) {
    *out = BitsToF32(UnpackU32Bits(slot));
    return true;
  } else if constexpr (std::is_same_v<T, double>) {
    *out = BitsToF64(UnpackU64Bits(slot));
    return true;
  } else if constexpr (std::is_same_v<T, bool>) {
    *out = (UnpackI32(slot) != 0);
    return true;
  } else if constexpr (std::is_same_v<T, const char*>) {
    uint32_t ref = UnpackRef(slot);
    if (ref == kNullRef) {
      *out = nullptr;
      return true;
    }
    HeapObject* obj = heap.Get(ref);
    if (!obj || obj->header.kind != ObjectKind::String) {
      if (out_error) *out_error = "core.dl.call string argument is not a string";
      return false;
    }
    owned_strings.push_back(U16ToAscii(ReadString(obj)));
    *out = owned_strings.back().c_str();
    return true;
  }
  if (out_error) *out_error = "core.dl.call unsupported argument type conversion";
  return false;
}

template <typename T>
bool PackDlReturn(T value, Heap& heap, Slot* out_ret, std::string* out_error) {
  if (!out_ret) return false;
  if constexpr (std::is_same_v<T, int8_t>) {
    *out_ret = PackI32(static_cast<int32_t>(value));
    return true;
  } else if constexpr (std::is_same_v<T, int16_t>) {
    *out_ret = PackI32(static_cast<int32_t>(value));
    return true;
  } else if constexpr (std::is_same_v<T, int32_t>) {
    *out_ret = PackI32(value);
    return true;
  } else if constexpr (std::is_same_v<T, int64_t>) {
    *out_ret = PackI64(value);
    return true;
  } else if constexpr (std::is_same_v<T, uint8_t>) {
    *out_ret = PackI32(static_cast<int32_t>(value));
    return true;
  } else if constexpr (std::is_same_v<T, uint16_t>) {
    *out_ret = PackI32(static_cast<int32_t>(value));
    return true;
  } else if constexpr (std::is_same_v<T, uint32_t>) {
    *out_ret = PackI32(static_cast<int32_t>(value));
    return true;
  } else if constexpr (std::is_same_v<T, uint64_t>) {
    *out_ret = PackI64(static_cast<int64_t>(value));
    return true;
  } else if constexpr (std::is_same_v<T, float>) {
    *out_ret = PackF32Bits(F32ToBits(value));
    return true;
  } else if constexpr (std::is_same_v<T, double>) {
    *out_ret = PackF64Bits(F64ToBits(value));
    return true;
  } else if constexpr (std::is_same_v<T, bool>) {
    *out_ret = PackI32(value ? 1 : 0);
    return true;
  } else if constexpr (std::is_same_v<T, const char*>) {
    if (!value) {
      *out_ret = PackRef(kNullRef);
      return true;
    }
    uint32_t handle = CreateString(heap, AsciiToU16(value));
    *out_ret = PackRef(handle);
    return true;
  }
  if (out_error) *out_error = "core.dl.call unsupported return type conversion";
  return false;
}

template <typename Ret, typename... Args>
bool InvokeDlFunctionTyped(int64_t ptr_bits,
                           const std::vector<Slot>& args,
                           size_t arg_base,
                           Heap& heap,
                           Slot* out_ret,
                           std::string* out_error) {
  std::vector<std::string> owned_strings;
  owned_strings.reserve(sizeof...(Args));
  std::tuple<std::decay_t<Args>...> converted{};
  bool ok = true;
  size_t index = 0;
  auto convert_one = [&](auto& dst) {
    if (!ok) return;
    using ArgT = std::decay_t<decltype(dst)>;
    if (arg_base + index >= args.size()) {
      ok = false;
      if (out_error) *out_error = "core.dl.call arg index out of range";
      return;
    }
    if (!ConvertDlArg<ArgT>(args[arg_base + index], heap, owned_strings, &dst, out_error)) {
      ok = false;
      return;
    }
    ++index;
  };
  std::apply([&](auto&... vals) { (convert_one(vals), ...); }, converted);
  if (!ok) return false;
  using Fn = Ret (*)(Args...);
  Fn fn = reinterpret_cast<Fn>(ptr_bits);
  Ret value = std::apply([&](auto... vals) -> Ret { return fn(vals...); }, converted);
  return PackDlReturn<Ret>(value, heap, out_ret, out_error);
}

template <typename... Args>
bool InvokeDlFunctionVoidTyped(int64_t ptr_bits,
                               const std::vector<Slot>& args,
                               size_t arg_base,
                               Heap& heap,
                               std::string* out_error) {
  std::vector<std::string> owned_strings;
  owned_strings.reserve(sizeof...(Args));
  std::tuple<std::decay_t<Args>...> converted{};
  bool ok = true;
  size_t index = 0;
  auto convert_one = [&](auto& dst) {
    if (!ok) return;
    using ArgT = std::decay_t<decltype(dst)>;
    if (arg_base + index >= args.size()) {
      ok = false;
      if (out_error) *out_error = "core.dl.call arg index out of range";
      return;
    }
    if (!ConvertDlArg<ArgT>(args[arg_base + index], heap, owned_strings, &dst, out_error)) {
      ok = false;
      return;
    }
    ++index;
  };
  std::apply([&](auto&... vals) { (convert_one(vals), ...); }, converted);
  if (!ok) return false;
  using Fn = void (*)(Args...);
  Fn fn = reinterpret_cast<Fn>(ptr_bits);
  std::apply([&](auto... vals) { fn(vals...); }, converted);
  return true;
}

#define SIMPLE_DL_FOREACH_TYPE(X) \
  X(TypeKind::I8, int8_t)         \
  X(TypeKind::I16, int16_t)       \
  X(TypeKind::I32, int32_t)       \
  X(TypeKind::I64, int64_t)       \
  X(TypeKind::U8, uint8_t)        \
  X(TypeKind::U16, uint16_t)      \
  X(TypeKind::U32, uint32_t)      \
  X(TypeKind::U64, uint64_t)      \
  X(TypeKind::F32, float)         \
  X(TypeKind::F64, double)        \
  X(TypeKind::Bool, bool)         \
  X(TypeKind::Char, uint8_t)      \
  X(TypeKind::String, const char*)

template <typename Ret>
bool DispatchDlCall1(TypeKind arg0_kind,
                     int64_t ptr_bits,
                     const std::vector<Slot>& args,
                     size_t arg_base,
                     Heap& heap,
                     Slot* out_ret,
                     std::string* out_error) {
  switch (arg0_kind) {
#define SIMPLE_DL_CASE_ARG0(kind, cpp_type) \
    case kind:                              \
      return InvokeDlFunctionTyped<Ret, cpp_type>(ptr_bits, args, arg_base, heap, out_ret, out_error);
    SIMPLE_DL_FOREACH_TYPE(SIMPLE_DL_CASE_ARG0)
#undef SIMPLE_DL_CASE_ARG0
    default:
      if (out_error) *out_error = "core.dl.call unsupported parameter type";
      return false;
  }
}

template <typename Arg0>
bool DispatchDlCall2Arg1Void(TypeKind arg1_kind,
                             int64_t ptr_bits,
                             const std::vector<Slot>& args,
                             size_t arg_base,
                             Heap& heap,
                             std::string* out_error) {
  switch (arg1_kind) {
#define SIMPLE_DL_CASE_ARG1_VOID(kind, cpp_type) \
    case kind:                                   \
      return InvokeDlFunctionVoidTyped<Arg0, cpp_type>(ptr_bits, args, arg_base, heap, out_error);
    SIMPLE_DL_FOREACH_TYPE(SIMPLE_DL_CASE_ARG1_VOID)
#undef SIMPLE_DL_CASE_ARG1_VOID
    default:
      if (out_error) *out_error = "core.dl.call unsupported parameter type";
      return false;
  }
}

bool DispatchDlCall1Void(TypeKind arg0_kind,
                         int64_t ptr_bits,
                         const std::vector<Slot>& args,
                         size_t arg_base,
                         Heap& heap,
                         std::string* out_error) {
  switch (arg0_kind) {
#define SIMPLE_DL_CASE_ARG0_VOID(kind, cpp_type) \
    case kind:                                   \
      return InvokeDlFunctionVoidTyped<cpp_type>(ptr_bits, args, arg_base, heap, out_error);
    SIMPLE_DL_FOREACH_TYPE(SIMPLE_DL_CASE_ARG0_VOID)
#undef SIMPLE_DL_CASE_ARG0_VOID
    default:
      if (out_error) *out_error = "core.dl.call unsupported parameter type";
      return false;
  }
}

template <typename Ret, typename Arg0>
bool DispatchDlCall2Arg1(TypeKind arg1_kind,
                         int64_t ptr_bits,
                         const std::vector<Slot>& args,
                         size_t arg_base,
                         Heap& heap,
                         Slot* out_ret,
                         std::string* out_error) {
  switch (arg1_kind) {
#define SIMPLE_DL_CASE_ARG1(kind, cpp_type) \
    case kind:                              \
      return InvokeDlFunctionTyped<Ret, Arg0, cpp_type>(ptr_bits, args, arg_base, heap, out_ret, out_error);
    SIMPLE_DL_FOREACH_TYPE(SIMPLE_DL_CASE_ARG1)
#undef SIMPLE_DL_CASE_ARG1
    default:
      if (out_error) *out_error = "core.dl.call unsupported parameter type";
      return false;
  }
}

template <typename Ret>
bool DispatchDlCall2(TypeKind arg0_kind,
                     TypeKind arg1_kind,
                     int64_t ptr_bits,
                     const std::vector<Slot>& args,
                     size_t arg_base,
                     Heap& heap,
                     Slot* out_ret,
                     std::string* out_error) {
  switch (arg0_kind) {
#define SIMPLE_DL_CASE_ARG0_2(kind, cpp_type) \
    case kind:                                \
      return DispatchDlCall2Arg1<Ret, cpp_type>(arg1_kind, ptr_bits, args, arg_base, heap, out_ret, out_error);
    SIMPLE_DL_FOREACH_TYPE(SIMPLE_DL_CASE_ARG0_2)
#undef SIMPLE_DL_CASE_ARG0_2
    default:
      if (out_error) *out_error = "core.dl.call unsupported parameter type";
      return false;
  }
}

bool DispatchDlCall2Void(TypeKind arg0_kind,
                         TypeKind arg1_kind,
                         int64_t ptr_bits,
                         const std::vector<Slot>& args,
                         size_t arg_base,
                         Heap& heap,
                         std::string* out_error) {
  switch (arg0_kind) {
#define SIMPLE_DL_CASE_ARG0_2_VOID(kind, cpp_type) \
    case kind:                                     \
      return DispatchDlCall2Arg1Void<cpp_type>(arg1_kind, ptr_bits, args, arg_base, heap, out_error);
    SIMPLE_DL_FOREACH_TYPE(SIMPLE_DL_CASE_ARG0_2_VOID)
#undef SIMPLE_DL_CASE_ARG0_2_VOID
    default:
      if (out_error) *out_error = "core.dl.call unsupported parameter type";
      return false;
  }
}

bool DispatchDynamicDlCall(int64_t ptr_bits,
                           TypeKind ret_kind,
                           const std::vector<TypeKind>& arg_kinds,
                           const std::vector<Slot>& args,
                           size_t arg_base,
                           Heap& heap,
                           Slot* out_ret,
                           std::string* out_error) {
  if (arg_kinds.size() > 2) {
    if (out_error) *out_error = "core.dl.call currently supports up to 2 parameters";
    return false;
  }
  for (TypeKind kind : arg_kinds) {
    if (!IsDlCallScalarKind(kind, false)) {
      if (out_error) *out_error = "core.dl.call unsupported parameter type";
      return false;
    }
  }
  if (!IsDlCallScalarKind(ret_kind, true)) {
    if (out_error) *out_error = "core.dl.call unsupported return type";
    return false;
  }
  if (ret_kind == TypeKind::Unspecified) {
    if (arg_kinds.empty()) return InvokeDlFunctionVoidTyped<>(ptr_bits, args, arg_base, heap, out_error);
    if (arg_kinds.size() == 1) {
      return DispatchDlCall1Void(arg_kinds[0], ptr_bits, args, arg_base, heap, out_error);
    }
    return DispatchDlCall2Void(arg_kinds[0], arg_kinds[1], ptr_bits, args, arg_base, heap, out_error);
  }
  if (!out_ret) {
    if (out_error) *out_error = "core.dl.call missing return slot";
    return false;
  }
  if (arg_kinds.empty()) {
    switch (ret_kind) {
#define SIMPLE_DL_CASE_RET0(kind, cpp_type) \
      case kind:                            \
        return InvokeDlFunctionTyped<cpp_type>(ptr_bits, args, arg_base, heap, out_ret, out_error);
      SIMPLE_DL_FOREACH_TYPE(SIMPLE_DL_CASE_RET0)
#undef SIMPLE_DL_CASE_RET0
      default:
        if (out_error) *out_error = "core.dl.call unsupported return type";
        return false;
    }
  }
  if (arg_kinds.size() == 1) {
    switch (ret_kind) {
#define SIMPLE_DL_CASE_RET1(kind, cpp_type) \
      case kind:                            \
        return DispatchDlCall1<cpp_type>(arg_kinds[0], ptr_bits, args, arg_base, heap, out_ret, out_error);
      SIMPLE_DL_FOREACH_TYPE(SIMPLE_DL_CASE_RET1)
#undef SIMPLE_DL_CASE_RET1
      default:
        if (out_error) *out_error = "core.dl.call unsupported return type";
        return false;
    }
  }
  switch (ret_kind) {
#define SIMPLE_DL_CASE_RET2(kind, cpp_type) \
    case kind:                              \
      return DispatchDlCall2<cpp_type>(arg_kinds[0], arg_kinds[1], ptr_bits, args, arg_base, heap, out_ret, out_error);
    SIMPLE_DL_FOREACH_TYPE(SIMPLE_DL_CASE_RET2)
#undef SIMPLE_DL_CASE_RET2
    default:
      if (out_error) *out_error = "core.dl.call unsupported return type";
      return false;
  }
}

#undef SIMPLE_DL_FOREACH_TYPE

std::string ReadConstPoolString(const SbcModule& module, uint32_t offset) {
  if (offset >= module.const_pool.size()) return {};
  std::string out;
  for (size_t pos = offset; pos < module.const_pool.size(); ++pos) {
    char c = static_cast<char>(module.const_pool[pos]);
    if (c == '\0') break;
    out.push_back(c);
  }
  return out;
}

std::u16string AsciiToU16(const std::string& text) {
  std::u16string out;
  out.reserve(text.size());
  for (unsigned char c : text) {
    out.push_back(static_cast<char16_t>(c));
  }
  return out;
}

std::string U16ToAscii(const std::u16string& text) {
  std::string out;
  out.reserve(text.size());
  for (char16_t c : text) {
    if (c <= 0x7Fu) {
      out.push_back(static_cast<char>(c));
    } else {
      out.push_back('?');
    }
  }
  return out;
}

struct Frame {
  size_t func_index = 0;
  size_t return_pc = 0;
  size_t stack_base = 0;
  uint32_t closure_ref = kNullRef;
  uint32_t line = 0;
  uint32_t column = 0;
  size_t locals_base = 0;
  uint16_t locals_count = 0;
};

struct JitStub {
  bool active = false;
  bool compiled = false;
  bool disabled = false;
};

struct TrapContext {
  Frame* current = nullptr;
  const std::vector<Frame>* call_stack = nullptr;
  const SbcModule* module = nullptr;
  size_t pc = 0;
  size_t func_start = 0;
  uint8_t last_opcode = 0xFF;
};

thread_local TrapContext* g_trap_ctx = nullptr;

struct TrapContextGuard {
  TrapContext* prev = nullptr;
  explicit TrapContextGuard(TrapContext* ctx) {
    prev = g_trap_ctx;
    g_trap_ctx = ctx;
  }
  ~TrapContextGuard() {
    g_trap_ctx = prev;
  }
};

int32_t ReadI32(const std::vector<uint8_t>& code, size_t& pc) {
  uint32_t v = static_cast<uint32_t>(code[pc]) |
               (static_cast<uint32_t>(code[pc + 1]) << 8) |
               (static_cast<uint32_t>(code[pc + 2]) << 16) |
               (static_cast<uint32_t>(code[pc + 3]) << 24);
  pc += 4;
  return static_cast<int32_t>(v);
}

int64_t ReadI64(const std::vector<uint8_t>& code, size_t& pc) {
  uint64_t v = static_cast<uint64_t>(code[pc]) |
               (static_cast<uint64_t>(code[pc + 1]) << 8) |
               (static_cast<uint64_t>(code[pc + 2]) << 16) |
               (static_cast<uint64_t>(code[pc + 3]) << 24) |
               (static_cast<uint64_t>(code[pc + 4]) << 32) |
               (static_cast<uint64_t>(code[pc + 5]) << 40) |
               (static_cast<uint64_t>(code[pc + 6]) << 48) |
               (static_cast<uint64_t>(code[pc + 7]) << 56);
  pc += 8;
  return static_cast<int64_t>(v);
}

uint32_t ReadU32(const std::vector<uint8_t>& code, size_t& pc) {
  uint32_t v = static_cast<uint32_t>(code[pc]) |
               (static_cast<uint32_t>(code[pc + 1]) << 8) |
               (static_cast<uint32_t>(code[pc + 2]) << 16) |
               (static_cast<uint32_t>(code[pc + 3]) << 24);
  pc += 4;
  return v;
}

uint64_t ReadU64(const std::vector<uint8_t>& code, size_t& pc) {
  uint64_t v = static_cast<uint64_t>(code[pc]) |
               (static_cast<uint64_t>(code[pc + 1]) << 8) |
               (static_cast<uint64_t>(code[pc + 2]) << 16) |
               (static_cast<uint64_t>(code[pc + 3]) << 24) |
               (static_cast<uint64_t>(code[pc + 4]) << 32) |
               (static_cast<uint64_t>(code[pc + 5]) << 40) |
               (static_cast<uint64_t>(code[pc + 6]) << 48) |
               (static_cast<uint64_t>(code[pc + 7]) << 56);
  pc += 8;
  return v;
}

uint16_t ReadU16(const std::vector<uint8_t>& code, size_t& pc) {
  uint16_t v = static_cast<uint16_t>(code[pc]) |
               (static_cast<uint16_t>(code[pc + 1]) << 8);
  pc += 2;
  return v;
}

uint8_t ReadU8(const std::vector<uint8_t>& code, size_t& pc) {
  return code[pc++];
}

Slot Pop(std::vector<Slot>& stack) {
  Slot v = stack.back();
  stack.pop_back();
  return v;
}

void Push(std::vector<Slot>& stack, Slot v) {
  stack.push_back(v);
}

uint32_t ReadU32Payload(const std::vector<uint8_t>& payload, size_t offset) {
  return static_cast<uint32_t>(payload[offset]) |
         (static_cast<uint32_t>(payload[offset + 1]) << 8) |
         (static_cast<uint32_t>(payload[offset + 2]) << 16) |
         (static_cast<uint32_t>(payload[offset + 3]) << 24);
}

uint64_t ReadU64Payload(const std::vector<uint8_t>& payload, size_t offset) {
  return static_cast<uint64_t>(payload[offset]) |
         (static_cast<uint64_t>(payload[offset + 1]) << 8) |
         (static_cast<uint64_t>(payload[offset + 2]) << 16) |
         (static_cast<uint64_t>(payload[offset + 3]) << 24) |
         (static_cast<uint64_t>(payload[offset + 4]) << 32) |
         (static_cast<uint64_t>(payload[offset + 5]) << 40) |
         (static_cast<uint64_t>(payload[offset + 6]) << 48) |
         (static_cast<uint64_t>(payload[offset + 7]) << 56);
}

void WriteU32Payload(std::vector<uint8_t>& payload, size_t offset, uint32_t value) {
  payload[offset + 0] = static_cast<uint8_t>(value & 0xFF);
  payload[offset + 1] = static_cast<uint8_t>((value >> 8) & 0xFF);
  payload[offset + 2] = static_cast<uint8_t>((value >> 16) & 0xFF);
  payload[offset + 3] = static_cast<uint8_t>((value >> 24) & 0xFF);
}

void WriteU64Payload(std::vector<uint8_t>& payload, size_t offset, uint64_t value) {
  payload[offset + 0] = static_cast<uint8_t>(value & 0xFF);
  payload[offset + 1] = static_cast<uint8_t>((value >> 8) & 0xFF);
  payload[offset + 2] = static_cast<uint8_t>((value >> 16) & 0xFF);
  payload[offset + 3] = static_cast<uint8_t>((value >> 24) & 0xFF);
  payload[offset + 4] = static_cast<uint8_t>((value >> 32) & 0xFF);
  payload[offset + 5] = static_cast<uint8_t>((value >> 40) & 0xFF);
  payload[offset + 6] = static_cast<uint8_t>((value >> 48) & 0xFF);
  payload[offset + 7] = static_cast<uint8_t>((value >> 56) & 0xFF);
}

uint16_t ReadU16Payload(const std::vector<uint8_t>& payload, size_t offset) {
  return static_cast<uint16_t>(payload[offset]) |
         (static_cast<uint16_t>(payload[offset + 1]) << 8);
}

void WriteU16Payload(std::vector<uint8_t>& payload, size_t offset, uint16_t value) {
  payload[offset + 0] = static_cast<uint8_t>(value & 0xFF);
  payload[offset + 1] = static_cast<uint8_t>((value >> 8) & 0xFF);
}

uint32_t CreateString(Heap& heap, const std::u16string& text) {
  uint32_t length = static_cast<uint32_t>(text.size());
  uint32_t size = 4 + length * 2;
  uint32_t handle = heap.Allocate(ObjectKind::String, 0, size);
  HeapObject* obj = heap.Get(handle);
  if (!obj) return 0xFFFFFFFFu;
  WriteU32Payload(obj->payload, 0, length);
  size_t offset = 4;
  for (uint32_t i = 0; i < length; ++i) {
    WriteU16Payload(obj->payload, offset, text[i]);
    offset += 2;
  }
  return handle;
}

std::u16string ReadString(const HeapObject* obj) {
  if (!obj || obj->header.kind != ObjectKind::String) return {};
  uint32_t length = ReadU32Payload(obj->payload, 0);
  std::u16string out;
  out.resize(length);
  size_t offset = 4;
  for (uint32_t i = 0; i < length; ++i) {
    out[i] = static_cast<char16_t>(ReadU16Payload(obj->payload, offset));
    offset += 2;
  }
  return out;
}


ExecResult Trap(const std::string& message) {
  ExecResult result;
  result.status = ExecStatus::Trapped;
  if (!g_trap_ctx || !g_trap_ctx->current) {
    result.error = message;
    return result;
  }
  auto get_method_name = [&](size_t func_index) -> std::string {
    if (!g_trap_ctx->module) return {};
    const auto& module = *g_trap_ctx->module;
    if (func_index >= module.functions.size()) return {};
    uint32_t method_id = module.functions[func_index].method_id;
    if (method_id >= module.methods.size()) return {};
    uint32_t name_offset = module.methods[method_id].name_str;
    if (name_offset >= module.const_pool.size()) return {};
    std::string out;
    for (size_t pos = name_offset; pos < module.const_pool.size(); ++pos) {
      char c = static_cast<char>(module.const_pool[pos]);
      if (c == '\0') break;
      out.push_back(c);
    }
    return out;
  };
  std::ostringstream out;
  out << message;
  const Frame* current = g_trap_ctx->current;
  out << " (func " << current->func_index;
  if (g_trap_ctx->pc >= g_trap_ctx->func_start) {
    out << " pc " << (g_trap_ctx->pc - g_trap_ctx->func_start);
  }
  if (g_trap_ctx->last_opcode != 0xFF) {
    out << " last_op 0x";
    static const char kHex[] = "0123456789ABCDEF";
    out << kHex[(g_trap_ctx->last_opcode >> 4) & 0xF];
    out << kHex[g_trap_ctx->last_opcode & 0xF];
    const char* op_name = OpCodeName(g_trap_ctx->last_opcode);
    if (op_name && op_name[0] != '\0') {
      out << " " << op_name;
    }
  }
  if (g_trap_ctx->module && g_trap_ctx->last_opcode != 0xFF) {
    const auto& code = g_trap_ctx->module->code;
    size_t op_pc = g_trap_ctx->pc;
    auto read_u32 = [&](size_t offset, uint32_t& out_val) -> bool {
      if (offset + 4 > code.size()) return false;
      out_val = static_cast<uint32_t>(code[offset]) |
                (static_cast<uint32_t>(code[offset + 1]) << 8) |
                (static_cast<uint32_t>(code[offset + 2]) << 16) |
                (static_cast<uint32_t>(code[offset + 3]) << 24);
      return true;
    };
    auto read_i32 = [&](size_t offset, int32_t& out_val) -> bool {
      uint32_t raw = 0;
      if (!read_u32(offset, raw)) return false;
      out_val = static_cast<int32_t>(raw);
      return true;
    };
    if (g_trap_ctx->last_opcode == static_cast<uint8_t>(OpCode::Call)) {
      uint32_t func_id = 0;
      uint32_t arg_count = 0;
      if (read_u32(op_pc + 1, func_id) && (op_pc + 5) < code.size()) {
        arg_count = code[op_pc + 5];
        out << " operands call func_id=" << func_id << " arg_count=" << arg_count;
      }
    } else if (g_trap_ctx->last_opcode == static_cast<uint8_t>(OpCode::Jmp) ||
               g_trap_ctx->last_opcode == static_cast<uint8_t>(OpCode::JmpTrue) ||
               g_trap_ctx->last_opcode == static_cast<uint8_t>(OpCode::JmpFalse)) {
      int32_t rel = 0;
      if (read_i32(op_pc + 1, rel)) {
        int64_t next_pc = static_cast<int64_t>(op_pc + 1 + 4);
        int64_t target = next_pc + rel;
        out << " operands rel=" << rel;
        if (g_trap_ctx->func_start <= static_cast<size_t>(target)) {
          out << " target_pc=" << (target - static_cast<int64_t>(g_trap_ctx->func_start));
        } else {
          out << " target_pc=" << target;
        }
      }
    } else if (g_trap_ctx->last_opcode == static_cast<uint8_t>(OpCode::JmpTable)) {
      uint32_t const_id = 0;
      int32_t def_rel = 0;
      if (read_u32(op_pc + 1, const_id) && read_i32(op_pc + 5, def_rel)) {
        int64_t next_pc = static_cast<int64_t>(op_pc + 1 + 8);
        int64_t target = next_pc + def_rel;
        out << " operands table_const=" << const_id << " default_rel=" << def_rel;
        if (g_trap_ctx->func_start <= static_cast<size_t>(target)) {
          out << " default_target_pc=" << (target - static_cast<int64_t>(g_trap_ctx->func_start));
        } else {
          out << " default_target_pc=" << target;
        }
      }
    }
  }
  if (current->line > 0) {
    out << " line " << current->line;
    if (current->column > 0) out << ":" << current->column;
  }
  std::string name = get_method_name(current->func_index);
  if (!name.empty()) {
    out << " name " << name;
  }
  out << ")";
  if (g_trap_ctx->call_stack && !g_trap_ctx->call_stack->empty()) {
    out << " stack:";
    for (auto it = g_trap_ctx->call_stack->rbegin(); it != g_trap_ctx->call_stack->rend(); ++it) {
      out << " <- func " << it->func_index;
      std::string caller_name = get_method_name(it->func_index);
      if (!caller_name.empty()) {
        out << " " << caller_name;
      }
      if (it->line > 0) {
        out << " " << it->line;
        if (it->column > 0) out << ":" << it->column;
      }
    }
  }
  result.error = out.str();
  return result;
}

} // namespace

ExecResult ExecuteModule(const SbcModule& module) {
  return ExecuteModule(module, true, true, ExecOptions{});
}

ExecResult ExecuteModule(const SbcModule& module, bool verify) {
  return ExecuteModule(module, verify, true, ExecOptions{});
}

ExecResult ExecuteModule(const SbcModule& module, bool verify, bool enable_jit) {
  return ExecuteModule(module, verify, enable_jit, ExecOptions{});
}

ExecResult ExecuteModule(const SbcModule& module, bool verify, bool enable_jit, const ExecOptions& options) {
  Simple::Byte::VerifyResult vr = Simple::Byte::VerifyModule(module);
  if (verify && !vr.ok) return Trap(vr.error);
  bool have_meta = vr.ok;
  if (module.functions.empty()) return Trap("no functions to execute");
  if (module.header.entry_method_id == 0xFFFFFFFFu) return Trap("no entry point");

  Heap heap;
  ScratchArena scratch_arena;
  scratch_arena.SetRequireScope(true);
  std::vector<Slot> globals(module.globals.size());
  std::vector<Slot> locals_arena;
  std::vector<Slot> jit_stack;
  std::vector<Slot> jit_locals;
  std::vector<uint32_t> call_counts(module.functions.size(), 0);
  std::vector<JitTier> jit_tiers(module.functions.size(), JitTier::None);
  std::vector<JitStub> jit_stubs(module.functions.size());
  std::vector<uint64_t> opcode_counts(256, 0);
  std::vector<uint32_t> compile_counts(module.functions.size(), 0);
  std::vector<uint32_t> func_opcode_counts(module.functions.size(), 0);
  std::vector<uint64_t> compile_ticks_tier0(module.functions.size(), 0);
  std::vector<uint64_t> compile_ticks_tier1(module.functions.size(), 0);
  std::vector<uint32_t> jit_dispatch_counts(module.functions.size(), 0);
  std::vector<uint32_t> jit_compiled_exec_counts(module.functions.size(), 0);
  std::vector<uint32_t> jit_tier1_exec_counts(module.functions.size(), 0);
  std::vector<std::FILE*> open_files;
  std::string dl_last_error;
  uint64_t compile_tick = 0;
  auto handle_import_call = [&](uint32_t func_id, const std::vector<Slot>& args, Slot& out_ret,
                                bool& out_has_ret, std::string& out_error) -> bool {
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
    std::string mod = ReadConstPoolString(module, row.module_name_str);
    std::string sym = ReadConstPoolString(module, row.symbol_name_str);
    if (mod.empty() || sym.empty()) {
      out_error = "import name invalid";
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
    if (mod == "core.os") {
      if (sym == "args_count") {
        if (IsI32LikeImportType(ret_kind)) {
          out_ret = PackI32(static_cast<int32_t>(options.argv.size()));
          return true;
        }
        out_error = "core.os.args_count return type mismatch";
        return false;
      }
      if (sym == "args_get" || sym == "env_get") {
        if (!IsStringLikeImportType(ret_kind)) {
          out_error = "core.os ref return type mismatch";
          return false;
        }
        if (sym == "args_get") {
          if (args.size() != 1) {
            out_error = "core.os.args_get arg count mismatch";
            return false;
          }
          int32_t index = UnpackI32(args[0]);
          if (index < 0 || static_cast<size_t>(index) >= options.argv.size()) {
            out_ret = PackRef(kNullRef);
            return true;
          }
          uint32_t handle = CreateString(heap, AsciiToU16(options.argv[static_cast<size_t>(index)]));
          out_ret = PackRef(handle);
          return true;
        }
        if (sym == "env_get") {
          if (args.size() != 1) {
            out_error = "core.os.env_get arg count mismatch";
            return false;
          }
          uint32_t name_ref = UnpackRef(args[0]);
          if (name_ref == kNullRef) {
            out_ret = PackRef(kNullRef);
            return true;
          }
          HeapObject* name_obj = heap.Get(name_ref);
          std::u16string name_u16 = ReadString(name_obj);
          std::string name = U16ToAscii(name_u16);
          if (name.empty()) {
            out_ret = PackRef(kNullRef);
            return true;
          }
          const char* value = std::getenv(name.c_str());
          if (!value) {
            out_ret = PackRef(kNullRef);
            return true;
          }
          uint32_t handle = CreateString(heap, AsciiToU16(value));
          out_ret = PackRef(handle);
          return true;
        }
        out_ret = PackRef(kNullRef);
        return true;
      }
      if (sym == "cwd_get") {
        if (!IsStringLikeImportType(ret_kind)) {
          out_error = "core.os.cwd_get return type mismatch";
          return false;
        }
        try {
          std::string cwd = std::filesystem::current_path().u8string();
          uint32_t handle = CreateString(heap, AsciiToU16(cwd));
          out_ret = PackRef(handle);
          return true;
        } catch (...) {
          out_ret = PackRef(kNullRef);
          return true;
        }
      }
      if (sym == "time_mono_ns" || sym == "time_wall_ns") {
        if (!IsI64LikeImportType(ret_kind)) {
          out_error = "core.os time return type mismatch";
          return false;
        }
        if (sym == "time_mono_ns") {
          auto now = std::chrono::steady_clock::now().time_since_epoch();
          auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(now).count();
          out_ret = PackI64(static_cast<int64_t>(ns));
          return true;
        }
        auto now = std::chrono::system_clock::now().time_since_epoch();
        auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(now).count();
        out_ret = PackI64(static_cast<int64_t>(ns));
        return true;
      }
      if (sym == "sleep_ms") {
        out_has_ret = false;
        if (args.size() != 1) {
          out_error = "core.os.sleep_ms arg count mismatch";
          return false;
        }
        int32_t ms = UnpackI32(args[0]);
        if (ms > 0) {
          std::this_thread::sleep_for(std::chrono::milliseconds(ms));
        }
        return true;
      }
    }
    if (mod == "core.fs") {
      if (sym == "open") {
        if (!IsI32LikeImportType(ret_kind)) {
          out_error = "core.fs return type mismatch";
          return false;
        }
        if (args.size() != 2) {
          out_error = "core.fs.open arg count mismatch";
          return false;
        }
        uint32_t path_ref = UnpackRef(args[0]);
        int32_t flags = UnpackI32(args[1]);
        if (path_ref == kNullRef) {
          out_ret = PackI32(-1);
          return true;
        }
        HeapObject* path_obj = heap.Get(path_ref);
        if (!path_obj || path_obj->header.kind != ObjectKind::String) {
          out_ret = PackI32(-1);
          return true;
        }
        std::string path = U16ToAscii(ReadString(path_obj));
        const char* mode = "rb";
        if (flags & 0x2) {
          mode = (flags & 0x1) ? "ab" : "ab";
        } else if (flags & 0x1) {
          mode = "wb";
        } else {
          mode = "rb";
        }
        std::FILE* f = std::fopen(path.c_str(), mode);
        if (!f) {
          out_ret = PackI32(-1);
          return true;
        }
        open_files.push_back(f);
        out_ret = PackI32(static_cast<int32_t>(open_files.size() - 1));
        return true;
      }
      if (sym == "read" || sym == "write") {
        if (!IsI32LikeImportType(ret_kind)) {
          out_error = "core.fs return type mismatch";
          return false;
        }
        if (args.size() != 3) {
          out_error = "core.fs io arg count mismatch";
          return false;
        }
        int32_t fd = UnpackI32(args[0]);
        uint32_t buf_ref = UnpackRef(args[1]);
        int32_t len = UnpackI32(args[2]);
        if (fd < 0 || static_cast<size_t>(fd) >= open_files.size()) {
          out_ret = PackI32(-1);
          return true;
        }
        std::FILE* f = open_files[static_cast<size_t>(fd)];
        if (!f || buf_ref == kNullRef || len < 0) {
          out_ret = PackI32(-1);
          return true;
        }
        HeapObject* buf_obj = heap.Get(buf_ref);
        if (!buf_obj || buf_obj->header.kind != ObjectKind::Array) {
          out_ret = PackI32(-1);
          return true;
        }
        uint32_t length = ReadU32Payload(buf_obj->payload, 0);
        uint32_t max_len = length;
        uint32_t req = static_cast<uint32_t>(len);
        if (req > max_len) req = max_len;
        ScratchScope scratch_scope(scratch_arena);
        uint8_t* tmp = nullptr;
        if (req > 0) {
          tmp = scratch_arena.Allocate(req, 1);
          if (!tmp) {
            out_ret = PackI32(-1);
            return true;
          }
          if (sym == "read") {
            std::memset(tmp, 0, req);
          }
        }
        if (sym == "read") {
          size_t got = (req > 0) ? std::fread(tmp, 1, req, f) : 0;
          for (size_t i = 0; i < got; ++i) {
            WriteU32Payload(buf_obj->payload, 4 + i * 4, tmp[i]);
          }
          out_ret = PackI32(static_cast<int32_t>(got));
          return true;
        }
        for (size_t i = 0; i < req; ++i) {
          tmp[i] = static_cast<uint8_t>(ReadU32Payload(buf_obj->payload, 4 + i * 4));
        }
        size_t wrote = (req > 0) ? std::fwrite(tmp, 1, req, f) : 0;
        out_ret = PackI32(static_cast<int32_t>(wrote));
        return true;
      }
      if (sym == "close") {
        out_has_ret = false;
        if (args.size() != 1) {
          out_error = "core.fs.close arg count mismatch";
          return false;
        }
        int32_t fd = UnpackI32(args[0]);
        if (fd < 0 || static_cast<size_t>(fd) >= open_files.size()) {
          return true;
        }
        std::FILE* f = open_files[static_cast<size_t>(fd)];
        if (f) {
          std::fclose(f);
          open_files[static_cast<size_t>(fd)] = nullptr;
        }
        return true;
      }
    }
    if (mod == "core.log") {
      if (sym == "log") {
        out_has_ret = false;
        return true;
      }
    }
    if (mod == "core.dl") {
      auto set_dl_error = [&](const std::string& text) {
        dl_last_error = text;
      };
      if (sym == "open") {
        if (!IsI64LikeImportType(ret_kind)) {
          out_error = "core.dl.open return type mismatch";
          return false;
        }
        if (args.size() != 1) {
          out_error = "core.dl.open arg count mismatch";
          return false;
        }
        uint32_t path_ref = UnpackRef(args[0]);
        if (path_ref == kNullRef) {
          set_dl_error("core.dl.open null path");
          out_ret = PackI64(0);
          return true;
        }
        HeapObject* path_obj = heap.Get(path_ref);
        if (!path_obj || path_obj->header.kind != ObjectKind::String) {
          set_dl_error("core.dl.open path not string");
          out_ret = PackI64(0);
          return true;
        }
        std::string path = U16ToAscii(ReadString(path_obj));
        dlerror();
        void* handle = dlopen(path.c_str(), RTLD_LAZY);
        if (!handle) {
          const char* err = dlerror();
          set_dl_error(err ? err : "core.dl.open failed");
          out_ret = PackI64(0);
          return true;
        }
        dl_last_error.clear();
        out_ret = PackI64(reinterpret_cast<int64_t>(handle));
        return true;
      }
      if (sym == "sym") {
        if (!IsI64LikeImportType(ret_kind)) {
          out_error = "core.dl.sym return type mismatch";
          return false;
        }
        if (args.size() != 2) {
          out_error = "core.dl.sym arg count mismatch";
          return false;
        }
        int64_t handle_bits = UnpackI64(args[0]);
        if (handle_bits == 0) {
          set_dl_error("core.dl.sym null handle");
          out_ret = PackI64(0);
          return true;
        }
        uint32_t name_ref = UnpackRef(args[1]);
        if (name_ref == kNullRef) {
          set_dl_error("core.dl.sym null name");
          out_ret = PackI64(0);
          return true;
        }
        HeapObject* name_obj = heap.Get(name_ref);
        if (!name_obj || name_obj->header.kind != ObjectKind::String) {
          set_dl_error("core.dl.sym name not string");
          out_ret = PackI64(0);
          return true;
        }
        std::string name = U16ToAscii(ReadString(name_obj));
        dlerror();
        void* sym_ptr = dlsym(reinterpret_cast<void*>(handle_bits), name.c_str());
        const char* err = dlerror();
        if (err) {
          set_dl_error(err);
          out_ret = PackI64(0);
          return true;
        }
        dl_last_error.clear();
        out_ret = PackI64(reinterpret_cast<int64_t>(sym_ptr));
        return true;
      }
      if (sym == "close") {
        if (!IsI32LikeImportType(ret_kind)) {
          out_error = "core.dl.close return type mismatch";
          return false;
        }
        if (args.size() != 1) {
          out_error = "core.dl.close arg count mismatch";
          return false;
        }
        int64_t handle_bits = UnpackI64(args[0]);
        if (handle_bits == 0) {
          set_dl_error("core.dl.close null handle");
          out_ret = PackI32(-1);
          return true;
        }
        int rc = dlclose(reinterpret_cast<void*>(handle_bits));
        if (rc != 0) {
          const char* err = dlerror();
          set_dl_error(err ? err : "core.dl.close failed");
          out_ret = PackI32(-1);
          return true;
        }
        dl_last_error.clear();
        out_ret = PackI32(0);
        return true;
      }
      if (sym == "last_error") {
        if (!IsStringLikeImportType(ret_kind)) {
          out_error = "core.dl.last_error return type mismatch";
          return false;
        }
        if (!args.empty()) {
          out_error = "core.dl.last_error arg count mismatch";
          return false;
        }
        if (dl_last_error.empty()) {
          out_ret = PackRef(kNullRef);
          return true;
        }
        uint32_t handle = CreateString(heap, AsciiToU16(dl_last_error));
        out_ret = PackRef(handle);
        return true;
      }
      if (sym.rfind("call$", 0) == 0) {
        if (sig.param_count == 0) {
          out_error = "core.dl.call signature missing function pointer";
          return false;
        }
        if (args.size() != sig.param_count) {
          out_error = "core.dl.call arg count mismatch";
          return false;
        }
        uint32_t ptr_type_id = module.param_types[sig.param_type_start];
        if (ptr_type_id >= module.types.size()) {
          out_error = "core.dl.call pointer type out of range";
          return false;
        }
        TypeKind ptr_kind = static_cast<TypeKind>(module.types[ptr_type_id].kind);
        if (ptr_kind != TypeKind::I64 && ptr_kind != TypeKind::U64) {
          out_error = "core.dl.call first parameter must be i64/u64";
          return false;
        }
        int64_t ptr_bits = UnpackI64(args[0]);
        if (ptr_bits == 0) {
          set_dl_error("core.dl.call null ptr");
          if (out_has_ret) {
            if (ret_kind == TypeKind::String || ret_kind == TypeKind::Ref) {
              out_ret = PackRef(kNullRef);
            } else if (ret_kind == TypeKind::I64 || ret_kind == TypeKind::U64) {
              out_ret = PackI64(0);
            } else if (ret_kind == TypeKind::F64) {
              out_ret = PackF64Bits(0);
            } else if (ret_kind == TypeKind::F32) {
              out_ret = PackF32Bits(0);
            } else {
              out_ret = PackI32(0);
            }
          }
          return true;
        }
        std::vector<TypeKind> arg_kinds;
        arg_kinds.reserve(sig.param_count > 0 ? static_cast<size_t>(sig.param_count - 1) : 0u);
        for (uint16_t i = 1; i < sig.param_count; ++i) {
          uint32_t type_id = module.param_types[sig.param_type_start + i];
          if (type_id >= module.types.size()) {
            out_error = "core.dl.call parameter type out of range";
            return false;
          }
          arg_kinds.push_back(static_cast<TypeKind>(module.types[type_id].kind));
        }
        if (!DispatchDynamicDlCall(ptr_bits, ret_kind, arg_kinds, args, 1, heap, &out_ret, &out_error)) {
          return false;
        }
        dl_last_error.clear();
        return true;
      }
    }
    out_error = "import not supported: " + mod + "." + sym;
    return false;
  };
  auto can_compile = [&](size_t func_index) -> bool {
    if (func_index >= module.functions.size()) return false;
    const auto& func = module.functions[func_index];
    if (func.method_id >= module.methods.size()) return false;
    const auto& method = module.methods[func.method_id];
    if (method.sig_id >= module.sigs.size()) return false;
    const auto& sig = module.sigs[method.sig_id];
    if (sig.param_count != 0) return false;
    size_t locals_count = 0;
    bool saw_enter = false;
    size_t pc = func.code_offset;
    size_t end_pc = func.code_offset + func.code_size;
    while (pc < end_pc) {
      uint8_t op = module.code[pc++];
      switch (static_cast<OpCode>(op)) {
        case OpCode::Enter: {
          if (pc + 2 > end_pc) return false;
          uint16_t locals = ReadU16(module.code, pc);
          if (saw_enter && locals_count != locals) return false;
          locals_count = locals;
          saw_enter = true;
          break;
        }
        case OpCode::Nop:
        case OpCode::Pop:
        case OpCode::Ret:
          break;
        case OpCode::ConstI32: {
          if (pc + 4 > end_pc) return false;
          pc += 4;
          break;
        }
        case OpCode::AddI32:
        case OpCode::SubI32:
        case OpCode::MulI32: {
          break;
        }
        case OpCode::DivI32: {
          break;
        }
        case OpCode::ModI32: {
          break;
        }
        case OpCode::CmpEqI32:
        case OpCode::CmpNeI32:
        case OpCode::CmpLtI32:
        case OpCode::CmpLeI32:
        case OpCode::CmpGtI32:
        case OpCode::CmpGeI32: {
          break;
        }
        case OpCode::BoolNot:
        case OpCode::BoolAnd:
        case OpCode::BoolOr: {
          break;
        }
        case OpCode::JmpTrue:
        case OpCode::JmpFalse: {
          if (pc + 4 > end_pc) return false;
          pc += 4;
          break;
        }
        case OpCode::Jmp: {
          if (pc + 4 > end_pc) return false;
          pc += 4;
          break;
        }
        case OpCode::LoadLocal: {
          if (!saw_enter || pc + 4 > end_pc) return false;
          uint32_t idx = ReadU32(module.code, pc);
          if (idx >= locals_count) return false;
          break;
        }
        case OpCode::StoreLocal: {
          if (!saw_enter || pc + 4 > end_pc) return false;
          uint32_t idx = ReadU32(module.code, pc);
          if (idx >= locals_count) return false;
          break;
        }
        default:
          return false;
      }
    }
    return true;
  };
  auto run_compiled = [&](size_t func_index, Slot& out_ret, bool& out_has_ret, std::string& error) -> bool {
    if (func_index >= module.functions.size()) {
      std::ostringstream out;
      out << "JIT compiled invalid function id op 0xFF Unknown pc 0";
      error = out.str();
      return false;
    }
    const auto& func = module.functions[func_index];
    size_t pc = func.code_offset;
    size_t end_pc = func.code_offset + func.code_size;
    jit_stack.clear();
    jit_locals.clear();
    std::vector<Slot>& local_stack = jit_stack;
    std::vector<Slot>& locals = jit_locals;
    bool saw_enter = false;
    bool skip_nops = (jit_tiers[func_index] == JitTier::Tier1);
    auto jit_fail = [&](const char* msg, uint8_t op, size_t inst_pc) -> bool {
      std::ostringstream out;
      out << msg << " op 0x";
      static const char kHex[] = "0123456789ABCDEF";
      out << kHex[(op >> 4) & 0xF] << kHex[op & 0xF];
      const char* name = OpCodeName(op);
      if (name && name[0] != '\0') {
        out << " " << name;
      }
      if (inst_pc >= func.code_offset) {
        out << " pc " << (inst_pc - func.code_offset);
      }
      auto read_u32 = [&](size_t offset, uint32_t& out_val) -> bool {
        if (offset + 4 > module.code.size()) return false;
        out_val = static_cast<uint32_t>(module.code[offset]) |
                  (static_cast<uint32_t>(module.code[offset + 1]) << 8) |
                  (static_cast<uint32_t>(module.code[offset + 2]) << 16) |
                  (static_cast<uint32_t>(module.code[offset + 3]) << 24);
        return true;
      };
      auto read_i32 = [&](size_t offset, int32_t& out_val) -> bool {
        uint32_t raw = 0;
        if (!read_u32(offset, raw)) return false;
        out_val = static_cast<int32_t>(raw);
        return true;
      };
      if (inst_pc + 1 < module.code.size()) {
        if (op == static_cast<uint8_t>(OpCode::Call)) {
          uint32_t func_id = 0;
          uint32_t arg_count = 0;
          if (read_u32(inst_pc + 1, func_id) && (inst_pc + 5) < module.code.size()) {
            arg_count = module.code[inst_pc + 5];
            out << " operands call func_id=" << func_id << " arg_count=" << arg_count;
          }
        } else if (op == static_cast<uint8_t>(OpCode::Jmp) ||
                   op == static_cast<uint8_t>(OpCode::JmpTrue) ||
                   op == static_cast<uint8_t>(OpCode::JmpFalse)) {
          int32_t rel = 0;
          if (read_i32(inst_pc + 1, rel)) {
            int64_t next_pc = static_cast<int64_t>(inst_pc + 1 + 4);
            int64_t target = next_pc + rel;
            out << " operands rel=" << rel;
            if (func.code_offset <= static_cast<size_t>(target)) {
              out << " target_pc=" << (target - static_cast<int64_t>(func.code_offset));
            } else {
              out << " target_pc=" << target;
            }
          }
        } else if (op == static_cast<uint8_t>(OpCode::JmpTable)) {
          uint32_t const_id = 0;
          int32_t def_rel = 0;
          if (read_u32(inst_pc + 1, const_id) && read_i32(inst_pc + 5, def_rel)) {
            int64_t next_pc = static_cast<int64_t>(inst_pc + 1 + 8);
            int64_t target = next_pc + def_rel;
            out << " operands table_const=" << const_id << " default_rel=" << def_rel;
            if (func.code_offset <= static_cast<size_t>(target)) {
              out << " default_target_pc=" << (target - static_cast<int64_t>(func.code_offset));
            } else {
              out << " default_target_pc=" << target;
            }
          }
        }
      }
      error = out.str();
      return false;
    };
    while (pc < end_pc) {
      uint8_t op = module.code[pc++];
      size_t inst_pc = pc - 1;
      switch (static_cast<OpCode>(op)) {
        case OpCode::Enter: {
          if (pc + 2 > end_pc) {
            return jit_fail("JIT compiled ENTER out of bounds", op, inst_pc);
          }
          uint16_t locals_count = ReadU16(module.code, pc);
          if (!saw_enter) {
            locals.assign(locals_count, 0);
            saw_enter = true;
          } else if (locals.size() != locals_count) {
            return jit_fail("JIT compiled locals mismatch", op, inst_pc);
          }
          break;
        }
        case OpCode::Nop:
          if (skip_nops) {
            break;
          }
          break;
        case OpCode::ConstI32: {
          if (pc + 4 > end_pc) {
            return jit_fail("JIT compiled CONST_I32 out of bounds", op, inst_pc);
          }
          int32_t value = ReadI32(module.code, pc);
          local_stack.push_back(PackI32(value));
          break;
        }
        case OpCode::AddI32: {
          if (local_stack.size() < 2) {
            return jit_fail("JIT compiled ADD_I32 underflow", op, inst_pc);
          }
          int32_t b = UnpackI32(local_stack.back());
          local_stack.pop_back();
          int32_t a = UnpackI32(local_stack.back());
          local_stack.pop_back();
          local_stack.push_back(PackI32(static_cast<int32_t>(a + b)));
          break;
        }
        case OpCode::SubI32: {
          if (local_stack.size() < 2) {
            return jit_fail("JIT compiled SUB_I32 underflow", op, inst_pc);
          }
          int32_t b = UnpackI32(local_stack.back());
          local_stack.pop_back();
          int32_t a = UnpackI32(local_stack.back());
          local_stack.pop_back();
          local_stack.push_back(PackI32(static_cast<int32_t>(a - b)));
          break;
        }
        case OpCode::MulI32: {
          if (local_stack.size() < 2) {
            return jit_fail("JIT compiled MUL_I32 underflow", op, inst_pc);
          }
          int32_t b = UnpackI32(local_stack.back());
          local_stack.pop_back();
          int32_t a = UnpackI32(local_stack.back());
          local_stack.pop_back();
          local_stack.push_back(PackI32(static_cast<int32_t>(a * b)));
          break;
        }
        case OpCode::DivI32: {
          if (local_stack.size() < 2) {
            return jit_fail("JIT compiled DIV_I32 underflow", op, inst_pc);
          }
          int32_t b = UnpackI32(local_stack.back());
          local_stack.pop_back();
          int32_t a = UnpackI32(local_stack.back());
          local_stack.pop_back();
          if (b == 0) {
            return jit_fail("JIT compiled DIV_I32 by zero", op, inst_pc);
          }
          local_stack.push_back(PackI32(static_cast<int32_t>(a / b)));
          break;
        }
        case OpCode::ModI32: {
          if (local_stack.size() < 2) {
            return jit_fail("JIT compiled MOD_I32 underflow", op, inst_pc);
          }
          int32_t b = UnpackI32(local_stack.back());
          local_stack.pop_back();
          int32_t a = UnpackI32(local_stack.back());
          local_stack.pop_back();
          if (b == 0) {
            return jit_fail("JIT compiled MOD_I32 by zero", op, inst_pc);
          }
          local_stack.push_back(PackI32(static_cast<int32_t>(a % b)));
          break;
        }
        case OpCode::CmpEqI32:
        case OpCode::CmpNeI32:
        case OpCode::CmpLtI32:
        case OpCode::CmpLeI32:
        case OpCode::CmpGtI32:
        case OpCode::CmpGeI32: {
          if (local_stack.size() < 2) {
            return jit_fail("JIT compiled CMP_I32 underflow", op, inst_pc);
          }
          Slot rhs = local_stack.back();
          local_stack.pop_back();
          Slot lhs = local_stack.back();
          local_stack.pop_back();
          int32_t a = UnpackI32(lhs);
          int32_t b = UnpackI32(rhs);
          bool result = false;
          switch (static_cast<OpCode>(op)) {
            case OpCode::CmpEqI32:
              result = (a == b);
              break;
            case OpCode::CmpNeI32:
              result = (a != b);
              break;
            case OpCode::CmpLtI32:
              result = (a < b);
              break;
            case OpCode::CmpLeI32:
              result = (a <= b);
              break;
            case OpCode::CmpGtI32:
              result = (a > b);
              break;
            case OpCode::CmpGeI32:
              result = (a >= b);
              break;
            default:
              break;
          }
          local_stack.push_back(PackI32(result ? 1 : 0));
          break;
        }
        case OpCode::BoolNot: {
          if (local_stack.empty()) {
            return jit_fail("JIT compiled BOOL_NOT underflow", op, inst_pc);
          }
          Slot v = local_stack.back();
          local_stack.pop_back();
          local_stack.push_back(PackI32(UnpackI32(v) == 0 ? 1 : 0));
          break;
        }
        case OpCode::BoolAnd:
        case OpCode::BoolOr: {
          if (local_stack.size() < 2) {
            return jit_fail("JIT compiled BOOL binop underflow", op, inst_pc);
          }
          Slot rhs = local_stack.back();
          local_stack.pop_back();
          Slot lhs = local_stack.back();
          local_stack.pop_back();
          bool result = false;
          if (static_cast<OpCode>(op) == OpCode::BoolAnd) {
            result = (UnpackI32(lhs) != 0) && (UnpackI32(rhs) != 0);
          } else {
            result = (UnpackI32(lhs) != 0) || (UnpackI32(rhs) != 0);
          }
          local_stack.push_back(PackI32(result ? 1 : 0));
          break;
        }
        case OpCode::JmpTrue:
        case OpCode::JmpFalse: {
          if (pc + 4 > end_pc) {
            return jit_fail("JIT compiled JMP out of bounds", op, inst_pc);
          }
          int32_t rel = ReadI32(module.code, pc);
          if (local_stack.empty()) {
            return jit_fail("JIT compiled JMP underflow", op, inst_pc);
          }
          Slot cond = local_stack.back();
          local_stack.pop_back();
          bool take = UnpackI32(cond) != 0;
          if (static_cast<OpCode>(op) == OpCode::JmpFalse) {
            take = !take;
          }
          if (take) {
            int64_t next = static_cast<int64_t>(pc) + rel;
            if (next < static_cast<int64_t>(func.code_offset) || next > static_cast<int64_t>(end_pc)) {
              std::ostringstream out;
              out << "JIT compiled JMP out of bounds rel=" << rel << " target=" << next;
              return jit_fail(out.str().c_str(), op, inst_pc);
            }
            pc = static_cast<size_t>(next);
          }
          break;
        }
        case OpCode::Jmp: {
          if (pc + 4 > end_pc) {
            return jit_fail("JIT compiled JMP out of bounds", op, inst_pc);
          }
          int32_t rel = ReadI32(module.code, pc);
          int64_t next = static_cast<int64_t>(pc) + rel;
          if (next < static_cast<int64_t>(func.code_offset) || next > static_cast<int64_t>(end_pc)) {
            std::ostringstream out;
            out << "JIT compiled JMP out of bounds rel=" << rel << " target=" << next;
            return jit_fail(out.str().c_str(), op, inst_pc);
          }
          pc = static_cast<size_t>(next);
          break;
        }
        case OpCode::LoadLocal: {
          if (pc + 4 > end_pc) {
            return jit_fail("JIT compiled LOAD_LOCAL out of bounds", op, inst_pc);
          }
          uint32_t idx = ReadU32(module.code, pc);
          if (idx >= locals.size()) {
            return jit_fail("JIT compiled LOAD_LOCAL invalid index", op, inst_pc);
          }
          local_stack.push_back(locals[idx]);
          break;
        }
        case OpCode::StoreLocal: {
          if (pc + 4 > end_pc) {
            return jit_fail("JIT compiled STORE_LOCAL out of bounds", op, inst_pc);
          }
          uint32_t idx = ReadU32(module.code, pc);
          if (idx >= locals.size()) {
            return jit_fail("JIT compiled STORE_LOCAL invalid index", op, inst_pc);
          }
          if (local_stack.empty()) {
            return jit_fail("JIT compiled STORE_LOCAL underflow", op, inst_pc);
          }
          locals[idx] = local_stack.back();
          local_stack.pop_back();
          break;
        }
        case OpCode::Pop: {
          if (local_stack.empty()) {
            return jit_fail("JIT compiled POP underflow", op, inst_pc);
          }
          local_stack.pop_back();
          break;
        }
        case OpCode::Ret: {
          out_has_ret = false;
          if (!local_stack.empty()) {
            out_ret = local_stack.back();
            out_has_ret = true;
          }
          return true;
        }
        default:
          return jit_fail("JIT compiled unsupported opcode", op, inst_pc);
      }
    }
    return jit_fail("JIT compiled missing RET", static_cast<uint8_t>(OpCode::Ret), end_pc);
  };
  auto update_tier = [&](size_t func_index) {
    if (!enable_jit) return;
    if (func_index >= call_counts.size()) return;
    uint32_t count = ++call_counts[func_index];
    if (count >= kJitTier1Threshold) {
      if (jit_tiers[func_index] != JitTier::Tier1) {
        jit_tiers[func_index] = JitTier::Tier1;
        jit_stubs[func_index].active = true;
        jit_stubs[func_index].compiled = jit_stubs[func_index].disabled ? false : can_compile(func_index);
        compile_counts[func_index] += 1;
        compile_ticks_tier1[func_index] = ++compile_tick;
      }
    } else if (count >= kJitTier0Threshold) {
      if (jit_tiers[func_index] == JitTier::None) {
        jit_tiers[func_index] = JitTier::Tier0;
        jit_stubs[func_index].active = true;
        jit_stubs[func_index].compiled = jit_stubs[func_index].disabled ? false : can_compile(func_index);
        compile_counts[func_index] += 1;
        compile_ticks_tier0[func_index] = ++compile_tick;
      }
    }
  };
  auto finish = [&](ExecResult result) {
    result.jit_tiers = jit_tiers;
    result.call_counts = call_counts;
    result.opcode_counts = opcode_counts;
    result.compile_counts = compile_counts;
    result.func_opcode_counts = func_opcode_counts;
    result.compile_ticks_tier0 = compile_ticks_tier0;
    result.compile_ticks_tier1 = compile_ticks_tier1;
    result.jit_dispatch_counts = jit_dispatch_counts;
    result.jit_compiled_exec_counts = jit_compiled_exec_counts;
    result.jit_tier1_exec_counts = jit_tier1_exec_counts;
    return result;
  };
  auto read_const_string = [&](uint32_t const_id, Slot& out_value) -> bool {
    uint32_t kind = ReadU32Payload(module.const_pool, const_id);
    if (kind != 0) return false;
    if (const_id + 8 > module.const_pool.size()) return false;
    uint32_t str_offset = ReadU32Payload(module.const_pool, const_id + 4);
    if (str_offset >= module.const_pool.size()) return false;
    const char* base = reinterpret_cast<const char*>(module.const_pool.data() + str_offset);
    std::u16string text;
    for (size_t i = 0; str_offset + i < module.const_pool.size(); ++i) {
      char c = base[i];
      if (c == '\0') break;
      text.push_back(static_cast<char16_t>(static_cast<unsigned char>(c)));
    }
    uint32_t handle = CreateString(heap, text);
    if (handle == 0xFFFFFFFFu) return false;
    out_value = PackRef(handle);
    return true;
  };
  for (size_t i = 0; i < module.globals.size(); ++i) {
    uint32_t const_id = module.globals[i].init_const_id;
    if (const_id == 0xFFFFFFFFu) continue;
    if (const_id + 4 > module.const_pool.size()) return Trap("GLOBAL init const out of bounds");
    uint32_t kind = ReadU32Payload(module.const_pool, const_id);
    if (kind == 0) {
      Slot value = 0;
      if (!read_const_string(const_id, value)) return Trap("GLOBAL init string failed");
      globals[i] = value;
      continue;
    }
    if (kind == 3) {
      if (const_id + 8 > module.const_pool.size()) return Trap("GLOBAL init f32 out of bounds");
      uint32_t bits = ReadU32Payload(module.const_pool, const_id + 4);
      globals[i] = PackF32Bits(bits);
      continue;
    }
    if (kind == 4) {
      if (const_id + 12 > module.const_pool.size()) return Trap("GLOBAL init f64 out of bounds");
      uint64_t bits = ReadU64Payload(module.const_pool, const_id + 4);
      globals[i] = PackF64Bits(bits);
      continue;
    }
    return Trap("GLOBAL init const unsupported");
  }

  size_t entry_func_index = 0;
  bool found = false;
  for (size_t i = 0; i < module.functions.size(); ++i) {
    if (module.functions[i].method_id == module.header.entry_method_id) {
      entry_func_index = i;
      found = true;
      break;
    }
  }
  if (!found) return Trap("entry method not found in functions table");

  std::vector<Slot> stack;
  std::vector<Frame> call_stack;
  std::vector<Slot> call_args;

  auto alloc_locals = [&](uint16_t count) -> size_t {
    size_t base = locals_arena.size();
    locals_arena.resize(base + count);
    std::fill(locals_arena.begin() + base, locals_arena.end(), 0);
    return base;
  };
  auto setup_frame = [&](size_t func_index, size_t return_pc, size_t stack_base, uint32_t closure_ref) -> Frame {
    update_tier(func_index);
    Frame frame;
    frame.func_index = func_index;
    frame.return_pc = return_pc;
    frame.stack_base = stack_base;
    frame.closure_ref = closure_ref;
    frame.line = 0;
    frame.column = 0;
    uint32_t method_id = module.functions[func_index].method_id;
    if (method_id >= module.methods.size()) {
      frame.locals_base = 0;
      frame.locals_count = 0;
      return frame;
    }
    uint16_t local_count = module.methods[method_id].local_count;
    frame.locals_count = local_count;
    frame.locals_base = alloc_locals(local_count);
    return frame;
  };

  size_t func_start = module.functions[entry_func_index].code_offset;
  Frame current = setup_frame(entry_func_index, 0, 0, kNullRef);
  TrapContext trap_ctx;
  trap_ctx.current = &current;
  trap_ctx.call_stack = &call_stack;
  trap_ctx.module = &module;
  trap_ctx.pc = 0;
  trap_ctx.func_start = func_start;
  TrapContextGuard trap_guard(&trap_ctx);
  size_t pc = func_start;
  size_t end = func_start + module.functions[entry_func_index].code_size;

  size_t op_counter = 0;
  auto ref_bit_set = [&](const std::vector<uint8_t>& bits, size_t index) -> bool {
    size_t byte = index / 8;
    if (byte >= bits.size()) return false;
    return (bits[byte] & static_cast<uint8_t>(1u << (index % 8))) != 0;
  };
  auto find_stack_map = [&](size_t func_index, size_t pc_value) -> const Simple::Byte::StackMap* {
    if (!have_meta || func_index >= vr.methods.size()) return nullptr;
    const auto& maps = vr.methods[func_index].stack_maps;
    for (const auto& map : maps) {
      if (map.pc == pc_value) return &map;
    }
    return nullptr;
  };
  auto maybe_collect = [&]() {
    if (!have_meta) return;
    if (op_counter % 1000 != 0) return;
    const Simple::Byte::StackMap* stack_map = find_stack_map(current.func_index, pc);
    if (!stack_map) return;
    heap.ResetMarks();
    for (size_t i = 0; i < globals.size(); ++i) {
      if (ref_bit_set(vr.globals_ref_bits, i) && !IsNullRef(globals[i])) {
        heap.Mark(UnpackRef(globals[i]));
      }
    }
    for (size_t i = 0; i < stack_map->stack_height && i < stack.size(); ++i) {
      if (ref_bit_set(stack_map->ref_bits, i) && !IsNullRef(stack[i])) {
        heap.Mark(UnpackRef(stack[i]));
      }
    }
    for (const auto& f : call_stack) {
      if (f.func_index >= vr.methods.size()) continue;
      const auto& bits = vr.methods[f.func_index].locals_ref_bits;
      for (size_t i = 0; i < f.locals_count; ++i) {
        Slot v = locals_arena[f.locals_base + i];
        if (ref_bit_set(bits, i) && !IsNullRef(v)) {
          heap.Mark(UnpackRef(v));
        }
      }
    }
    if (current.func_index < vr.methods.size()) {
      const auto& bits = vr.methods[current.func_index].locals_ref_bits;
      for (size_t i = 0; i < current.locals_count; ++i) {
        Slot v = locals_arena[current.locals_base + i];
        if (ref_bit_set(bits, i) && !IsNullRef(v)) {
          heap.Mark(UnpackRef(v));
        }
      }
    }
    heap.Sweep();
  };

  while (pc < module.code.size()) {
    trap_ctx.pc = pc;
    trap_ctx.func_start = func_start;
    ++op_counter;
    maybe_collect();
    if (pc >= end) {
      if (call_stack.empty()) {
        ExecResult done;
        done.status = ExecStatus::Halted;
        return finish(done);
      }
      return Trap("pc out of bounds for function");
    }

    uint8_t opcode = module.code[pc++];
    trap_ctx.last_opcode = opcode;
    opcode_counts[opcode] += 1;
    if (current.func_index < func_opcode_counts.size()) {
      uint32_t& count = func_opcode_counts[current.func_index];
      count += 1;
      if (enable_jit && count >= kJitOpcodeThreshold && jit_tiers[current.func_index] == JitTier::None) {
        jit_tiers[current.func_index] = JitTier::Tier0;
        jit_stubs[current.func_index].active = true;
        jit_stubs[current.func_index].compiled =
            jit_stubs[current.func_index].disabled ? false : can_compile(current.func_index);
        compile_counts[current.func_index] += 1;
        compile_ticks_tier0[current.func_index] = ++compile_tick;
      }
    }
    switch (static_cast<OpCode>(opcode)) {
      case OpCode::Nop:
        break;
      case OpCode::Halt: {
        ExecResult result;
        result.status = ExecStatus::Halted;
        if (!stack.empty()) {
          result.exit_code = UnpackI32(stack.back());
        }
        return finish(result);
      }
      case OpCode::Trap:
        return Trap("TRAP");
      case OpCode::Breakpoint:
        break;
      case OpCode::Pop: {
        if (stack.empty()) return Trap("POP on empty stack");
        stack.pop_back();
        break;
      }
      case OpCode::Dup: {
        if (stack.empty()) return Trap("DUP on empty stack");
        stack.push_back(stack.back());
        break;
      }
      case OpCode::Dup2: {
        if (stack.size() < 2) return Trap("DUP2 on short stack");
        Slot b = stack[stack.size() - 1];
        Slot a = stack[stack.size() - 2];
        stack.push_back(a);
        stack.push_back(b);
        break;
      }
      case OpCode::Swap: {
        if (stack.size() < 2) return Trap("SWAP on short stack");
        Slot a = stack[stack.size() - 1];
        Slot b = stack[stack.size() - 2];
        stack[stack.size() - 1] = b;
        stack[stack.size() - 2] = a;
        break;
      }
      case OpCode::Rot: {
        if (stack.size() < 3) return Trap("ROT on short stack");
        Slot c = stack[stack.size() - 1];
        Slot b = stack[stack.size() - 2];
        Slot a = stack[stack.size() - 3];
        stack[stack.size() - 3] = b;
        stack[stack.size() - 2] = c;
        stack[stack.size() - 1] = a;
        break;
      }
      case OpCode::ConstI32: {
        int32_t value = ReadI32(module.code, pc);
        Push(stack, PackI32(value));
        break;
      }
      case OpCode::ConstI64: {
        int64_t value = ReadI64(module.code, pc);
        Push(stack, PackI64(value));
        break;
      }
      case OpCode::ConstU32: {
        uint32_t value = ReadU32(module.code, pc);
        Push(stack, PackI32(static_cast<int32_t>(value)));
        break;
      }
      case OpCode::ConstU64: {
        uint64_t value = ReadU64(module.code, pc);
        Push(stack, PackI64(static_cast<int64_t>(value)));
        break;
      }
      case OpCode::ConstI8: {
        int8_t value = static_cast<int8_t>(ReadU8(module.code, pc));
        Push(stack, PackI32(value));
        break;
      }
      case OpCode::ConstI16: {
        int16_t value = static_cast<int16_t>(ReadU16(module.code, pc));
        Push(stack, PackI32(value));
        break;
      }
      case OpCode::ConstU8: {
        uint8_t value = ReadU8(module.code, pc);
        Push(stack, PackI32(value));
        break;
      }
      case OpCode::ConstU16: {
        uint16_t value = ReadU16(module.code, pc);
        Push(stack, PackI32(value));
        break;
      }
      case OpCode::ConstF32: {
        uint32_t bits = ReadU32(module.code, pc);
        Push(stack, PackF32Bits(bits));
        break;
      }
      case OpCode::ConstF64: {
        uint64_t bits = ReadU64(module.code, pc);
        Push(stack, PackF64Bits(bits));
        break;
      }
      case OpCode::ConstI128:
      case OpCode::ConstU128: {
        uint32_t const_id = ReadU32(module.code, pc);
        if (const_id + 8 > module.const_pool.size()) return Trap("CONST_I128/U128 out of bounds");
        uint32_t kind = ReadU32Payload(module.const_pool, const_id);
        uint32_t want = (opcode == static_cast<uint8_t>(OpCode::ConstI128)) ? 1u : 2u;
        if (kind != want) return Trap("CONST_I128/U128 wrong const kind");
        uint32_t blob_offset = ReadU32Payload(module.const_pool, const_id + 4);
        if (blob_offset + 4 > module.const_pool.size()) return Trap("CONST_I128/U128 bad blob offset");
        uint32_t blob_len = ReadU32Payload(module.const_pool, blob_offset);
        if (blob_len < 16) return Trap("CONST_I128/U128 blob too small");
        if (blob_offset + 4 + blob_len > module.const_pool.size()) return Trap("CONST_I128/U128 blob out of bounds");
        Push(stack, PackRef(kNullRef));
        break;
      }
      case OpCode::ConstChar: {
        uint16_t value = ReadU16(module.code, pc);
        Push(stack, PackI32(value));
        break;
      }
      case OpCode::ConstBool: {
        uint8_t v = ReadU8(module.code, pc);
        Push(stack, PackI32(v ? 1 : 0));
        break;
      }
      case OpCode::ConstString: {
        uint32_t const_id = ReadU32(module.code, pc);
        if (const_id + 8 > module.const_pool.size()) return Trap("CONST_STRING out of bounds");
        uint32_t kind = ReadU32Payload(module.const_pool, const_id);
        if (kind != 0) return Trap("CONST_STRING wrong const kind");
        uint32_t str_offset = ReadU32Payload(module.const_pool, const_id + 4);
        if (str_offset >= module.const_pool.size()) return Trap("CONST_STRING bad offset");
        const char* base = reinterpret_cast<const char*>(module.const_pool.data() + str_offset);
        std::u16string text;
        for (size_t i = 0; str_offset + i < module.const_pool.size(); ++i) {
          char c = base[i];
          if (c == '\0') break;
          text.push_back(static_cast<char16_t>(static_cast<unsigned char>(c)));
        }
        uint32_t handle = CreateString(heap, text);
        if (handle == 0xFFFFFFFFu) return Trap("CONST_STRING allocation failed");
        Push(stack, PackRef(handle));
        break;
      }
      case OpCode::ConstNull: {
        Push(stack, PackRef(kNullRef));
        break;
      }
      case OpCode::LoadLocal: {
        uint32_t idx = ReadU32(module.code, pc);
        if (idx >= current.locals_count) return Trap("LOAD_LOCAL out of range");
        Push(stack, locals_arena[current.locals_base + idx]);
        break;
      }
      case OpCode::StoreLocal: {
        uint32_t idx = ReadU32(module.code, pc);
        if (idx >= current.locals_count) return Trap("STORE_LOCAL out of range");
        locals_arena[current.locals_base + idx] = Pop(stack);
        break;
      }
      case OpCode::LoadGlobal: {
        uint32_t idx = ReadU32(module.code, pc);
        if (idx >= globals.size()) return Trap("LOAD_GLOBAL out of range");
        Push(stack, globals[idx]);
        break;
      }
      case OpCode::StoreGlobal: {
        uint32_t idx = ReadU32(module.code, pc);
        if (idx >= globals.size()) return Trap("STORE_GLOBAL out of range");
        globals[idx] = Pop(stack);
        break;
      }
      case OpCode::LoadUpvalue: {
        uint32_t idx = ReadU32(module.code, pc);
        if (current.closure_ref == kNullRef) return Trap("LOAD_UPVALUE without closure");
        HeapObject* obj = heap.Get(current.closure_ref);
        if (!obj || obj->header.kind != ObjectKind::Closure) return Trap("LOAD_UPVALUE on non-closure");
        if (obj->payload.size() < 8) return Trap("LOAD_UPVALUE invalid closure payload");
        uint32_t count = ReadU32Payload(obj->payload, 4);
        if (idx >= count) return Trap("LOAD_UPVALUE out of bounds");
        size_t offset = 8 + static_cast<size_t>(idx) * 4;
        if (offset + 4 > obj->payload.size()) return Trap("LOAD_UPVALUE out of bounds");
        uint32_t handle = ReadU32Payload(obj->payload, offset);
        Push(stack, PackRef(handle));
        break;
      }
      case OpCode::StoreUpvalue: {
        uint32_t idx = ReadU32(module.code, pc);
        Slot v = Pop(stack);
        if (current.closure_ref == kNullRef) return Trap("STORE_UPVALUE without closure");
        HeapObject* obj = heap.Get(current.closure_ref);
        if (!obj || obj->header.kind != ObjectKind::Closure) return Trap("STORE_UPVALUE on non-closure");
        if (obj->payload.size() < 8) return Trap("STORE_UPVALUE invalid closure payload");
        uint32_t count = ReadU32Payload(obj->payload, 4);
        if (idx >= count) return Trap("STORE_UPVALUE out of bounds");
        size_t offset = 8 + static_cast<size_t>(idx) * 4;
        if (offset + 4 > obj->payload.size()) return Trap("STORE_UPVALUE out of bounds");
        WriteU32Payload(obj->payload, offset, UnpackRef(v));
        break;
      }
      case OpCode::NewObject: {
        uint32_t type_id = ReadU32(module.code, pc);
        if (type_id >= module.types.size()) return Trap("NEW_OBJECT bad type id");
        uint32_t size = module.types[type_id].size;
        uint32_t handle = heap.Allocate(ObjectKind::Artifact, type_id, size);
        Push(stack, PackRef(handle));
        break;
      }
      case OpCode::NewClosure: {
        uint32_t method_id = ReadU32(module.code, pc);
        uint8_t upvalue_count = ReadU8(module.code, pc);
        if (method_id >= module.methods.size()) return Trap("NEW_CLOSURE bad method id");
        uint32_t size = 8 + static_cast<uint32_t>(upvalue_count) * 4u;
        uint32_t handle = heap.Allocate(ObjectKind::Closure, method_id, size);
        HeapObject* obj = heap.Get(handle);
        if (!obj) return Trap("NEW_CLOSURE allocation failed");
        WriteU32Payload(obj->payload, 0, method_id);
        WriteU32Payload(obj->payload, 4, static_cast<uint32_t>(upvalue_count));
        if (stack.size() < upvalue_count) return Trap("NEW_CLOSURE stack underflow");
        for (int32_t i = static_cast<int32_t>(upvalue_count) - 1; i >= 0; --i) {
          Slot v = Pop(stack);
          WriteU32Payload(obj->payload, 8 + static_cast<uint32_t>(i) * 4u, UnpackRef(v));
        }
        Push(stack, PackRef(handle));
        break;
      }
      case OpCode::LoadField: {
        uint32_t field_id = ReadU32(module.code, pc);
        Slot v = Pop(stack);
        if (field_id >= module.fields.size()) return Trap("LOAD_FIELD bad field id");
        if (IsNullRef(v)) return Trap("LOAD_FIELD on non-ref");
        HeapObject* obj = heap.Get(UnpackRef(v));
        if (!obj || obj->header.kind != ObjectKind::Artifact) return Trap("LOAD_FIELD on non-object");
        uint32_t offset = module.fields[field_id].offset;
        if (offset + 4 > obj->payload.size()) return Trap("LOAD_FIELD out of bounds");
        int32_t value = static_cast<int32_t>(ReadU32Payload(obj->payload, offset));
        Push(stack, PackI32(value));
        break;
      }
      case OpCode::StoreField: {
        uint32_t field_id = ReadU32(module.code, pc);
        Slot value = Pop(stack);
        Slot v = Pop(stack);
        if (field_id >= module.fields.size()) return Trap("STORE_FIELD bad field id");
        if (IsNullRef(v)) return Trap("STORE_FIELD on non-ref");
        HeapObject* obj = heap.Get(UnpackRef(v));
        if (!obj || obj->header.kind != ObjectKind::Artifact) return Trap("STORE_FIELD on non-object");
        uint32_t offset = module.fields[field_id].offset;
        if (offset + 4 > obj->payload.size()) return Trap("STORE_FIELD out of bounds");
        WriteU32Payload(obj->payload, offset, static_cast<uint32_t>(UnpackI32(value)));
        break;
      }
      case OpCode::IsNull: {
        Slot v = Pop(stack);
        Push(stack, PackI32(IsNullRef(v) ? 1 : 0));
        break;
      }
      case OpCode::RefEq:
      case OpCode::RefNe: {
        Slot b = Pop(stack);
        Slot a = Pop(stack);
        bool out = (UnpackRef(a) == UnpackRef(b));
        if (opcode == static_cast<uint8_t>(OpCode::RefNe)) out = !out;
        Push(stack, PackI32(out ? 1 : 0));
        break;
      }
      case OpCode::TypeOf: {
        Slot v = Pop(stack);
        if (IsNullRef(v)) return Trap("TYPEOF on non-ref");
        HeapObject* obj = heap.Get(UnpackRef(v));
        if (!obj) return Trap("TYPEOF on invalid ref");
        Push(stack, PackI32(static_cast<int32_t>(obj->header.type_id)));
        break;
      }
      case OpCode::NewArray: {
        uint32_t type_id = ReadU32(module.code, pc);
        uint32_t length = ReadU32(module.code, pc);
        uint32_t size = 4 + length * 4;
        uint32_t handle = heap.Allocate(ObjectKind::Array, type_id, size);
        HeapObject* obj = heap.Get(handle);
        if (!obj) return Trap("NEW_ARRAY allocation failed");
        WriteU32Payload(obj->payload, 0, length);
        Push(stack, PackRef(handle));
        break;
      }
      case OpCode::NewArrayI64:
      case OpCode::NewArrayF64: {
        uint32_t type_id = ReadU32(module.code, pc);
        uint32_t length = ReadU32(module.code, pc);
        uint32_t size = 4 + length * 8;
        uint32_t handle = heap.Allocate(ObjectKind::Array, type_id, size);
        HeapObject* obj = heap.Get(handle);
        if (!obj) return Trap("NEW_ARRAY allocation failed");
        WriteU32Payload(obj->payload, 0, length);
        Push(stack, PackRef(handle));
        break;
      }
      case OpCode::NewArrayF32:
      case OpCode::NewArrayRef: {
        uint32_t type_id = ReadU32(module.code, pc);
        uint32_t length = ReadU32(module.code, pc);
        uint32_t size = 4 + length * 4;
        uint32_t handle = heap.Allocate(ObjectKind::Array, type_id, size);
        HeapObject* obj = heap.Get(handle);
        if (!obj) return Trap("NEW_ARRAY allocation failed");
        WriteU32Payload(obj->payload, 0, length);
        Push(stack, PackRef(handle));
        break;
      }
      case OpCode::ArrayLen: {
        Slot v = Pop(stack);
        if (IsNullRef(v)) return Trap("ARRAY_LEN on non-ref");
        HeapObject* obj = heap.Get(UnpackRef(v));
        if (!obj || obj->header.kind != ObjectKind::Array) return Trap("ARRAY_LEN on non-array");
        uint32_t length = ReadU32Payload(obj->payload, 0);
        Push(stack, PackI32(static_cast<int32_t>(length)));
        break;
      }
      case OpCode::ArrayGetI32: {
        Slot idx = Pop(stack);
        Slot v = Pop(stack);
        if (IsNullRef(v)) return Trap("ARRAY_GET on non-ref");
        HeapObject* obj = heap.Get(UnpackRef(v));
        if (!obj || obj->header.kind != ObjectKind::Array) return Trap("ARRAY_GET on non-array");
        uint32_t length = ReadU32Payload(obj->payload, 0);
        int32_t index = UnpackI32(idx);
        if (index < 0 || static_cast<uint32_t>(index) >= length) return Trap("ARRAY_GET out of bounds");
        size_t offset = 4 + static_cast<size_t>(index) * 4;
        int32_t value = static_cast<int32_t>(ReadU32Payload(obj->payload, offset));
        Push(stack, PackI32(value));
        break;
      }
      case OpCode::ArrayGetI64: {
        Slot idx = Pop(stack);
        Slot v = Pop(stack);
        if (IsNullRef(v)) return Trap("ARRAY_GET on non-ref");
        HeapObject* obj = heap.Get(UnpackRef(v));
        if (!obj || obj->header.kind != ObjectKind::Array) return Trap("ARRAY_GET on non-array");
        uint32_t length = ReadU32Payload(obj->payload, 0);
        int32_t index = UnpackI32(idx);
        if (index < 0 || static_cast<uint32_t>(index) >= length) return Trap("ARRAY_GET out of bounds");
        size_t offset = 4 + static_cast<size_t>(index) * 8;
        int64_t value = static_cast<int64_t>(ReadU64Payload(obj->payload, offset));
        Push(stack, PackI64(value));
        break;
      }
      case OpCode::ArrayGetF32: {
        Slot idx = Pop(stack);
        Slot v = Pop(stack);
        if (IsNullRef(v)) return Trap("ARRAY_GET on non-ref");
        HeapObject* obj = heap.Get(UnpackRef(v));
        if (!obj || obj->header.kind != ObjectKind::Array) return Trap("ARRAY_GET on non-array");
        uint32_t length = ReadU32Payload(obj->payload, 0);
        int32_t index = UnpackI32(idx);
        if (index < 0 || static_cast<uint32_t>(index) >= length) return Trap("ARRAY_GET out of bounds");
        size_t offset = 4 + static_cast<size_t>(index) * 4;
        uint32_t bits = ReadU32Payload(obj->payload, offset);
        Push(stack, PackF32Bits(bits));
        break;
      }
      case OpCode::ArrayGetF64: {
        Slot idx = Pop(stack);
        Slot v = Pop(stack);
        if (IsNullRef(v)) return Trap("ARRAY_GET on non-ref");
        HeapObject* obj = heap.Get(UnpackRef(v));
        if (!obj || obj->header.kind != ObjectKind::Array) return Trap("ARRAY_GET on non-array");
        uint32_t length = ReadU32Payload(obj->payload, 0);
        int32_t index = UnpackI32(idx);
        if (index < 0 || static_cast<uint32_t>(index) >= length) return Trap("ARRAY_GET out of bounds");
        size_t offset = 4 + static_cast<size_t>(index) * 8;
        uint64_t bits = ReadU64Payload(obj->payload, offset);
        Push(stack, PackF64Bits(bits));
        break;
      }
      case OpCode::ArrayGetRef: {
        Slot idx = Pop(stack);
        Slot v = Pop(stack);
        if (IsNullRef(v)) return Trap("ARRAY_GET on non-ref");
        HeapObject* obj = heap.Get(UnpackRef(v));
        if (!obj || obj->header.kind != ObjectKind::Array) return Trap("ARRAY_GET on non-array");
        uint32_t length = ReadU32Payload(obj->payload, 0);
        int32_t index = UnpackI32(idx);
        if (index < 0 || static_cast<uint32_t>(index) >= length) return Trap("ARRAY_GET out of bounds");
        size_t offset = 4 + static_cast<size_t>(index) * 4;
        uint32_t handle = ReadU32Payload(obj->payload, offset);
        Push(stack, PackRef(handle));
        break;
      }
      case OpCode::ArraySetI32: {
        Slot value = Pop(stack);
        Slot idx = Pop(stack);
        Slot v = Pop(stack);
        if (IsNullRef(v)) return Trap("ARRAY_SET on non-ref");
        HeapObject* obj = heap.Get(UnpackRef(v));
        if (!obj || obj->header.kind != ObjectKind::Array) return Trap("ARRAY_SET on non-array");
        uint32_t length = ReadU32Payload(obj->payload, 0);
        int32_t index = UnpackI32(idx);
        if (index < 0 || static_cast<uint32_t>(index) >= length) return Trap("ARRAY_SET out of bounds");
        size_t offset = 4 + static_cast<size_t>(index) * 4;
        WriteU32Payload(obj->payload, offset, static_cast<uint32_t>(UnpackI32(value)));
        break;
      }
      case OpCode::ArraySetI64: {
        Slot value = Pop(stack);
        Slot idx = Pop(stack);
        Slot v = Pop(stack);
        if (IsNullRef(v)) return Trap("ARRAY_SET on non-ref");
        HeapObject* obj = heap.Get(UnpackRef(v));
        if (!obj || obj->header.kind != ObjectKind::Array) return Trap("ARRAY_SET on non-array");
        uint32_t length = ReadU32Payload(obj->payload, 0);
        int32_t index = UnpackI32(idx);
        if (index < 0 || static_cast<uint32_t>(index) >= length) return Trap("ARRAY_SET out of bounds");
        size_t offset = 4 + static_cast<size_t>(index) * 8;
        WriteU64Payload(obj->payload, offset, static_cast<uint64_t>(UnpackI64(value)));
        break;
      }
      case OpCode::ArraySetF32: {
        Slot value = Pop(stack);
        Slot idx = Pop(stack);
        Slot v = Pop(stack);
        if (IsNullRef(v)) return Trap("ARRAY_SET on non-ref");
        HeapObject* obj = heap.Get(UnpackRef(v));
        if (!obj || obj->header.kind != ObjectKind::Array) return Trap("ARRAY_SET on non-array");
        uint32_t length = ReadU32Payload(obj->payload, 0);
        int32_t index = UnpackI32(idx);
        if (index < 0 || static_cast<uint32_t>(index) >= length) return Trap("ARRAY_SET out of bounds");
        size_t offset = 4 + static_cast<size_t>(index) * 4;
        WriteU32Payload(obj->payload, offset, UnpackU32Bits(value));
        break;
      }
      case OpCode::ArraySetF64: {
        Slot value = Pop(stack);
        Slot idx = Pop(stack);
        Slot v = Pop(stack);
        if (IsNullRef(v)) return Trap("ARRAY_SET on non-ref");
        HeapObject* obj = heap.Get(UnpackRef(v));
        if (!obj || obj->header.kind != ObjectKind::Array) return Trap("ARRAY_SET on non-array");
        uint32_t length = ReadU32Payload(obj->payload, 0);
        int32_t index = UnpackI32(idx);
        if (index < 0 || static_cast<uint32_t>(index) >= length) return Trap("ARRAY_SET out of bounds");
        size_t offset = 4 + static_cast<size_t>(index) * 8;
        WriteU64Payload(obj->payload, offset, UnpackU64Bits(value));
        break;
      }
      case OpCode::ArraySetRef: {
        Slot value = Pop(stack);
        Slot idx = Pop(stack);
        Slot v = Pop(stack);
        if (IsNullRef(v)) return Trap("ARRAY_SET on non-ref");
        HeapObject* obj = heap.Get(UnpackRef(v));
        if (!obj || obj->header.kind != ObjectKind::Array) return Trap("ARRAY_SET on non-array");
        uint32_t length = ReadU32Payload(obj->payload, 0);
        int32_t index = UnpackI32(idx);
        if (index < 0 || static_cast<uint32_t>(index) >= length) return Trap("ARRAY_SET out of bounds");
        size_t offset = 4 + static_cast<size_t>(index) * 4;
        WriteU32Payload(obj->payload, offset, UnpackRef(value));
        break;
      }
      case OpCode::NewList: {
        uint32_t type_id = ReadU32(module.code, pc);
        uint32_t capacity = ReadU32(module.code, pc);
        uint32_t size = 8 + capacity * 4;
        uint32_t handle = heap.Allocate(ObjectKind::List, type_id, size);
        HeapObject* obj = heap.Get(handle);
        if (!obj) return Trap("NEW_LIST allocation failed");
        WriteU32Payload(obj->payload, 0, 0);
        WriteU32Payload(obj->payload, 4, capacity);
        Push(stack, PackRef(handle));
        break;
      }
      case OpCode::NewListI64:
      case OpCode::NewListF64: {
        uint32_t type_id = ReadU32(module.code, pc);
        uint32_t capacity = ReadU32(module.code, pc);
        uint32_t size = 8 + capacity * 8;
        uint32_t handle = heap.Allocate(ObjectKind::List, type_id, size);
        HeapObject* obj = heap.Get(handle);
        if (!obj) return Trap("NEW_LIST allocation failed");
        WriteU32Payload(obj->payload, 0, 0);
        WriteU32Payload(obj->payload, 4, capacity);
        Push(stack, PackRef(handle));
        break;
      }
      case OpCode::NewListF32:
      case OpCode::NewListRef: {
        uint32_t type_id = ReadU32(module.code, pc);
        uint32_t capacity = ReadU32(module.code, pc);
        uint32_t size = 8 + capacity * 4;
        uint32_t handle = heap.Allocate(ObjectKind::List, type_id, size);
        HeapObject* obj = heap.Get(handle);
        if (!obj) return Trap("NEW_LIST allocation failed");
        WriteU32Payload(obj->payload, 0, 0);
        WriteU32Payload(obj->payload, 4, capacity);
        Push(stack, PackRef(handle));
        break;
      }
      case OpCode::ListLen: {
        Slot v = Pop(stack);
        if (IsNullRef(v)) return Trap("LIST_LEN on non-ref");
        HeapObject* obj = heap.Get(UnpackRef(v));
        if (!obj || obj->header.kind != ObjectKind::List) return Trap("LIST_LEN on non-list");
        uint32_t length = ReadU32Payload(obj->payload, 0);
        Push(stack, PackI32(static_cast<int32_t>(length)));
        break;
      }
      case OpCode::ListGetI32: {
        Slot idx = Pop(stack);
        Slot v = Pop(stack);
        if (IsNullRef(v)) return Trap("LIST_GET on non-ref");
        HeapObject* obj = heap.Get(UnpackRef(v));
        if (!obj || obj->header.kind != ObjectKind::List) return Trap("LIST_GET on non-list");
        uint32_t length = ReadU32Payload(obj->payload, 0);
        int32_t index = UnpackI32(idx);
        if (index < 0 || static_cast<uint32_t>(index) >= length) return Trap("LIST_GET out of bounds");
        size_t offset = 8 + static_cast<size_t>(index) * 4;
        int32_t value = static_cast<int32_t>(ReadU32Payload(obj->payload, offset));
        Push(stack, PackI32(value));
        break;
      }
      case OpCode::ListGetI64: {
        Slot idx = Pop(stack);
        Slot v = Pop(stack);
        if (IsNullRef(v)) return Trap("LIST_GET on non-ref");
        HeapObject* obj = heap.Get(UnpackRef(v));
        if (!obj || obj->header.kind != ObjectKind::List) return Trap("LIST_GET on non-list");
        uint32_t length = ReadU32Payload(obj->payload, 0);
        int32_t index = UnpackI32(idx);
        if (index < 0 || static_cast<uint32_t>(index) >= length) return Trap("LIST_GET out of bounds");
        size_t offset = 8 + static_cast<size_t>(index) * 8;
        int64_t value = static_cast<int64_t>(ReadU64Payload(obj->payload, offset));
        Push(stack, PackI64(value));
        break;
      }
      case OpCode::ListGetF32: {
        Slot idx = Pop(stack);
        Slot v = Pop(stack);
        if (IsNullRef(v)) return Trap("LIST_GET on non-ref");
        HeapObject* obj = heap.Get(UnpackRef(v));
        if (!obj || obj->header.kind != ObjectKind::List) return Trap("LIST_GET on non-list");
        uint32_t length = ReadU32Payload(obj->payload, 0);
        int32_t index = UnpackI32(idx);
        if (index < 0 || static_cast<uint32_t>(index) >= length) return Trap("LIST_GET out of bounds");
        size_t offset = 8 + static_cast<size_t>(index) * 4;
        uint32_t bits = ReadU32Payload(obj->payload, offset);
        Push(stack, PackF32Bits(bits));
        break;
      }
      case OpCode::ListGetF64: {
        Slot idx = Pop(stack);
        Slot v = Pop(stack);
        if (IsNullRef(v)) return Trap("LIST_GET on non-ref");
        HeapObject* obj = heap.Get(UnpackRef(v));
        if (!obj || obj->header.kind != ObjectKind::List) return Trap("LIST_GET on non-list");
        uint32_t length = ReadU32Payload(obj->payload, 0);
        int32_t index = UnpackI32(idx);
        if (index < 0 || static_cast<uint32_t>(index) >= length) return Trap("LIST_GET out of bounds");
        size_t offset = 8 + static_cast<size_t>(index) * 8;
        uint64_t bits = ReadU64Payload(obj->payload, offset);
        Push(stack, PackF64Bits(bits));
        break;
      }
      case OpCode::ListGetRef: {
        Slot idx = Pop(stack);
        Slot v = Pop(stack);
        if (IsNullRef(v)) return Trap("LIST_GET on non-ref");
        HeapObject* obj = heap.Get(UnpackRef(v));
        if (!obj || obj->header.kind != ObjectKind::List) return Trap("LIST_GET on non-list");
        uint32_t length = ReadU32Payload(obj->payload, 0);
        int32_t index = UnpackI32(idx);
        if (index < 0 || static_cast<uint32_t>(index) >= length) return Trap("LIST_GET out of bounds");
        size_t offset = 8 + static_cast<size_t>(index) * 4;
        uint32_t handle = ReadU32Payload(obj->payload, offset);
        Push(stack, PackRef(handle));
        break;
      }
      case OpCode::ListSetI32: {
        Slot value = Pop(stack);
        Slot idx = Pop(stack);
        Slot v = Pop(stack);
        if (IsNullRef(v)) return Trap("LIST_SET on non-ref");
        HeapObject* obj = heap.Get(UnpackRef(v));
        if (!obj || obj->header.kind != ObjectKind::List) return Trap("LIST_SET on non-list");
        uint32_t length = ReadU32Payload(obj->payload, 0);
        int32_t index = UnpackI32(idx);
        if (index < 0 || static_cast<uint32_t>(index) >= length) return Trap("LIST_SET out of bounds");
        size_t offset = 8 + static_cast<size_t>(index) * 4;
        WriteU32Payload(obj->payload, offset, static_cast<uint32_t>(UnpackI32(value)));
        break;
      }
      case OpCode::ListSetI64: {
        Slot value = Pop(stack);
        Slot idx = Pop(stack);
        Slot v = Pop(stack);
        if (IsNullRef(v)) return Trap("LIST_SET on non-ref");
        HeapObject* obj = heap.Get(UnpackRef(v));
        if (!obj || obj->header.kind != ObjectKind::List) return Trap("LIST_SET on non-list");
        uint32_t length = ReadU32Payload(obj->payload, 0);
        int32_t index = UnpackI32(idx);
        if (index < 0 || static_cast<uint32_t>(index) >= length) return Trap("LIST_SET out of bounds");
        size_t offset = 8 + static_cast<size_t>(index) * 8;
        WriteU64Payload(obj->payload, offset, static_cast<uint64_t>(UnpackI64(value)));
        break;
      }
      case OpCode::ListSetF32: {
        Slot value = Pop(stack);
        Slot idx = Pop(stack);
        Slot v = Pop(stack);
        if (IsNullRef(v)) return Trap("LIST_SET on non-ref");
        HeapObject* obj = heap.Get(UnpackRef(v));
        if (!obj || obj->header.kind != ObjectKind::List) return Trap("LIST_SET on non-list");
        uint32_t length = ReadU32Payload(obj->payload, 0);
        int32_t index = UnpackI32(idx);
        if (index < 0 || static_cast<uint32_t>(index) >= length) return Trap("LIST_SET out of bounds");
        size_t offset = 8 + static_cast<size_t>(index) * 4;
        WriteU32Payload(obj->payload, offset, UnpackU32Bits(value));
        break;
      }
      case OpCode::ListSetF64: {
        Slot value = Pop(stack);
        Slot idx = Pop(stack);
        Slot v = Pop(stack);
        if (IsNullRef(v)) return Trap("LIST_SET on non-ref");
        HeapObject* obj = heap.Get(UnpackRef(v));
        if (!obj || obj->header.kind != ObjectKind::List) return Trap("LIST_SET on non-list");
        uint32_t length = ReadU32Payload(obj->payload, 0);
        int32_t index = UnpackI32(idx);
        if (index < 0 || static_cast<uint32_t>(index) >= length) return Trap("LIST_SET out of bounds");
        size_t offset = 8 + static_cast<size_t>(index) * 8;
        WriteU64Payload(obj->payload, offset, UnpackU64Bits(value));
        break;
      }
      case OpCode::ListSetRef: {
        Slot value = Pop(stack);
        Slot idx = Pop(stack);
        Slot v = Pop(stack);
        if (IsNullRef(v)) return Trap("LIST_SET on non-ref");
        HeapObject* obj = heap.Get(UnpackRef(v));
        if (!obj || obj->header.kind != ObjectKind::List) return Trap("LIST_SET on non-list");
        uint32_t length = ReadU32Payload(obj->payload, 0);
        int32_t index = UnpackI32(idx);
        if (index < 0 || static_cast<uint32_t>(index) >= length) return Trap("LIST_SET out of bounds");
        size_t offset = 8 + static_cast<size_t>(index) * 4;
        WriteU32Payload(obj->payload, offset, UnpackRef(value));
        break;
      }
      case OpCode::ListPushI32: {
        Slot value = Pop(stack);
        Slot v = Pop(stack);
        if (IsNullRef(v)) return Trap("LIST_PUSH on non-ref");
        HeapObject* obj = heap.Get(UnpackRef(v));
        if (!obj || obj->header.kind != ObjectKind::List) return Trap("LIST_PUSH on non-list");
        uint32_t length = ReadU32Payload(obj->payload, 0);
        uint32_t capacity = ReadU32Payload(obj->payload, 4);
        if (length >= capacity) return Trap("LIST_PUSH overflow");
        size_t offset = 8 + static_cast<size_t>(length) * 4;
        WriteU32Payload(obj->payload, offset, static_cast<uint32_t>(UnpackI32(value)));
        WriteU32Payload(obj->payload, 0, length + 1);
        break;
      }
      case OpCode::ListPushI64: {
        Slot value = Pop(stack);
        Slot v = Pop(stack);
        if (IsNullRef(v)) return Trap("LIST_PUSH on non-ref");
        HeapObject* obj = heap.Get(UnpackRef(v));
        if (!obj || obj->header.kind != ObjectKind::List) return Trap("LIST_PUSH on non-list");
        uint32_t length = ReadU32Payload(obj->payload, 0);
        uint32_t capacity = ReadU32Payload(obj->payload, 4);
        if (length >= capacity) return Trap("LIST_PUSH overflow");
        size_t offset = 8 + static_cast<size_t>(length) * 8;
        WriteU64Payload(obj->payload, offset, static_cast<uint64_t>(UnpackI64(value)));
        WriteU32Payload(obj->payload, 0, length + 1);
        break;
      }
      case OpCode::ListPushF32: {
        Slot value = Pop(stack);
        Slot v = Pop(stack);
        if (IsNullRef(v)) return Trap("LIST_PUSH on non-ref");
        HeapObject* obj = heap.Get(UnpackRef(v));
        if (!obj || obj->header.kind != ObjectKind::List) return Trap("LIST_PUSH on non-list");
        uint32_t length = ReadU32Payload(obj->payload, 0);
        uint32_t capacity = ReadU32Payload(obj->payload, 4);
        if (length >= capacity) return Trap("LIST_PUSH overflow");
        size_t offset = 8 + static_cast<size_t>(length) * 4;
        WriteU32Payload(obj->payload, offset, UnpackU32Bits(value));
        WriteU32Payload(obj->payload, 0, length + 1);
        break;
      }
      case OpCode::ListPushF64: {
        Slot value = Pop(stack);
        Slot v = Pop(stack);
        if (IsNullRef(v)) return Trap("LIST_PUSH on non-ref");
        HeapObject* obj = heap.Get(UnpackRef(v));
        if (!obj || obj->header.kind != ObjectKind::List) return Trap("LIST_PUSH on non-list");
        uint32_t length = ReadU32Payload(obj->payload, 0);
        uint32_t capacity = ReadU32Payload(obj->payload, 4);
        if (length >= capacity) return Trap("LIST_PUSH overflow");
        size_t offset = 8 + static_cast<size_t>(length) * 8;
        WriteU64Payload(obj->payload, offset, UnpackU64Bits(value));
        WriteU32Payload(obj->payload, 0, length + 1);
        break;
      }
      case OpCode::ListPushRef: {
        Slot value = Pop(stack);
        Slot v = Pop(stack);
        if (IsNullRef(v)) return Trap("LIST_PUSH on non-ref");
        HeapObject* obj = heap.Get(UnpackRef(v));
        if (!obj || obj->header.kind != ObjectKind::List) return Trap("LIST_PUSH on non-list");
        uint32_t length = ReadU32Payload(obj->payload, 0);
        uint32_t capacity = ReadU32Payload(obj->payload, 4);
        if (length >= capacity) return Trap("LIST_PUSH overflow");
        size_t offset = 8 + static_cast<size_t>(length) * 4;
        WriteU32Payload(obj->payload, offset, UnpackRef(value));
        WriteU32Payload(obj->payload, 0, length + 1);
        break;
      }
      case OpCode::ListPopI32: {
        Slot v = Pop(stack);
        if (IsNullRef(v)) return Trap("LIST_POP on non-ref");
        HeapObject* obj = heap.Get(UnpackRef(v));
        if (!obj || obj->header.kind != ObjectKind::List) return Trap("LIST_POP on non-list");
        uint32_t length = ReadU32Payload(obj->payload, 0);
        if (length == 0) return Trap("LIST_POP empty");
        uint32_t index = length - 1;
        size_t offset = 8 + static_cast<size_t>(index) * 4;
        int32_t value = static_cast<int32_t>(ReadU32Payload(obj->payload, offset));
        WriteU32Payload(obj->payload, 0, length - 1);
        Push(stack, PackI32(value));
        break;
      }
      case OpCode::ListPopI64: {
        Slot v = Pop(stack);
        if (IsNullRef(v)) return Trap("LIST_POP on non-ref");
        HeapObject* obj = heap.Get(UnpackRef(v));
        if (!obj || obj->header.kind != ObjectKind::List) return Trap("LIST_POP on non-list");
        uint32_t length = ReadU32Payload(obj->payload, 0);
        if (length == 0) return Trap("LIST_POP empty");
        uint32_t index = length - 1;
        size_t offset = 8 + static_cast<size_t>(index) * 8;
        int64_t value = static_cast<int64_t>(ReadU64Payload(obj->payload, offset));
        WriteU32Payload(obj->payload, 0, length - 1);
        Push(stack, PackI64(value));
        break;
      }
      case OpCode::ListPopF32: {
        Slot v = Pop(stack);
        if (IsNullRef(v)) return Trap("LIST_POP on non-ref");
        HeapObject* obj = heap.Get(UnpackRef(v));
        if (!obj || obj->header.kind != ObjectKind::List) return Trap("LIST_POP on non-list");
        uint32_t length = ReadU32Payload(obj->payload, 0);
        if (length == 0) return Trap("LIST_POP empty");
        uint32_t index = length - 1;
        size_t offset = 8 + static_cast<size_t>(index) * 4;
        uint32_t bits = ReadU32Payload(obj->payload, offset);
        WriteU32Payload(obj->payload, 0, length - 1);
        Push(stack, PackF32Bits(bits));
        break;
      }
      case OpCode::ListPopF64: {
        Slot v = Pop(stack);
        if (IsNullRef(v)) return Trap("LIST_POP on non-ref");
        HeapObject* obj = heap.Get(UnpackRef(v));
        if (!obj || obj->header.kind != ObjectKind::List) return Trap("LIST_POP on non-list");
        uint32_t length = ReadU32Payload(obj->payload, 0);
        if (length == 0) return Trap("LIST_POP empty");
        uint32_t index = length - 1;
        size_t offset = 8 + static_cast<size_t>(index) * 8;
        uint64_t bits = ReadU64Payload(obj->payload, offset);
        WriteU32Payload(obj->payload, 0, length - 1);
        Push(stack, PackF64Bits(bits));
        break;
      }
      case OpCode::ListPopRef: {
        Slot v = Pop(stack);
        if (IsNullRef(v)) return Trap("LIST_POP on non-ref");
        HeapObject* obj = heap.Get(UnpackRef(v));
        if (!obj || obj->header.kind != ObjectKind::List) return Trap("LIST_POP on non-list");
        uint32_t length = ReadU32Payload(obj->payload, 0);
        if (length == 0) return Trap("LIST_POP empty");
        uint32_t index = length - 1;
        size_t offset = 8 + static_cast<size_t>(index) * 4;
        uint32_t handle = ReadU32Payload(obj->payload, offset);
        WriteU32Payload(obj->payload, 0, length - 1);
        Push(stack, PackRef(handle));
        break;
      }
      case OpCode::ListInsertI32: {
        Slot value = Pop(stack);
        Slot idx_val = Pop(stack);
        Slot v = Pop(stack);
        if (IsNullRef(v)) return Trap("LIST_INSERT on non-ref");
        HeapObject* obj = heap.Get(UnpackRef(v));
        if (!obj || obj->header.kind != ObjectKind::List) return Trap("LIST_INSERT on non-list");
        uint32_t length = ReadU32Payload(obj->payload, 0);
        uint32_t capacity = ReadU32Payload(obj->payload, 4);
        if (length >= capacity) return Trap("LIST_INSERT overflow");
        int32_t index = UnpackI32(idx_val);
        if (index < 0 || static_cast<uint32_t>(index) > length) return Trap("LIST_INSERT out of bounds");
        for (uint32_t i = length; i > static_cast<uint32_t>(index); --i) {
          size_t from = 8 + static_cast<size_t>(i - 1) * 4;
          size_t to = 8 + static_cast<size_t>(i) * 4;
          WriteU32Payload(obj->payload, to, ReadU32Payload(obj->payload, from));
        }
        size_t offset = 8 + static_cast<size_t>(index) * 4;
        WriteU32Payload(obj->payload, offset, static_cast<uint32_t>(UnpackI32(value)));
        WriteU32Payload(obj->payload, 0, length + 1);
        break;
      }
      case OpCode::ListInsertI64: {
        Slot value = Pop(stack);
        Slot idx_val = Pop(stack);
        Slot v = Pop(stack);
        if (IsNullRef(v)) return Trap("LIST_INSERT on non-ref");
        HeapObject* obj = heap.Get(UnpackRef(v));
        if (!obj || obj->header.kind != ObjectKind::List) return Trap("LIST_INSERT on non-list");
        uint32_t length = ReadU32Payload(obj->payload, 0);
        uint32_t capacity = ReadU32Payload(obj->payload, 4);
        if (length >= capacity) return Trap("LIST_INSERT overflow");
        int32_t index = UnpackI32(idx_val);
        if (index < 0 || static_cast<uint32_t>(index) > length) return Trap("LIST_INSERT out of bounds");
        for (uint32_t i = length; i > static_cast<uint32_t>(index); --i) {
          size_t from = 8 + static_cast<size_t>(i - 1) * 8;
          size_t to = 8 + static_cast<size_t>(i) * 8;
          WriteU64Payload(obj->payload, to, ReadU64Payload(obj->payload, from));
        }
        size_t offset = 8 + static_cast<size_t>(index) * 8;
        WriteU64Payload(obj->payload, offset, static_cast<uint64_t>(UnpackI64(value)));
        WriteU32Payload(obj->payload, 0, length + 1);
        break;
      }
      case OpCode::ListInsertF32: {
        Slot value = Pop(stack);
        Slot idx_val = Pop(stack);
        Slot v = Pop(stack);
        if (IsNullRef(v)) return Trap("LIST_INSERT on non-ref");
        HeapObject* obj = heap.Get(UnpackRef(v));
        if (!obj || obj->header.kind != ObjectKind::List) return Trap("LIST_INSERT on non-list");
        uint32_t length = ReadU32Payload(obj->payload, 0);
        uint32_t capacity = ReadU32Payload(obj->payload, 4);
        if (length >= capacity) return Trap("LIST_INSERT overflow");
        int32_t index = UnpackI32(idx_val);
        if (index < 0 || static_cast<uint32_t>(index) > length) return Trap("LIST_INSERT out of bounds");
        for (uint32_t i = length; i > static_cast<uint32_t>(index); --i) {
          size_t from = 8 + static_cast<size_t>(i - 1) * 4;
          size_t to = 8 + static_cast<size_t>(i) * 4;
          WriteU32Payload(obj->payload, to, ReadU32Payload(obj->payload, from));
        }
        size_t offset = 8 + static_cast<size_t>(index) * 4;
        WriteU32Payload(obj->payload, offset, UnpackU32Bits(value));
        WriteU32Payload(obj->payload, 0, length + 1);
        break;
      }
      case OpCode::ListInsertF64: {
        Slot value = Pop(stack);
        Slot idx_val = Pop(stack);
        Slot v = Pop(stack);
        if (IsNullRef(v)) return Trap("LIST_INSERT on non-ref");
        HeapObject* obj = heap.Get(UnpackRef(v));
        if (!obj || obj->header.kind != ObjectKind::List) return Trap("LIST_INSERT on non-list");
        uint32_t length = ReadU32Payload(obj->payload, 0);
        uint32_t capacity = ReadU32Payload(obj->payload, 4);
        if (length >= capacity) return Trap("LIST_INSERT overflow");
        int32_t index = UnpackI32(idx_val);
        if (index < 0 || static_cast<uint32_t>(index) > length) return Trap("LIST_INSERT out of bounds");
        for (uint32_t i = length; i > static_cast<uint32_t>(index); --i) {
          size_t from = 8 + static_cast<size_t>(i - 1) * 8;
          size_t to = 8 + static_cast<size_t>(i) * 8;
          WriteU64Payload(obj->payload, to, ReadU64Payload(obj->payload, from));
        }
        size_t offset = 8 + static_cast<size_t>(index) * 8;
        WriteU64Payload(obj->payload, offset, UnpackU64Bits(value));
        WriteU32Payload(obj->payload, 0, length + 1);
        break;
      }
      case OpCode::ListInsertRef: {
        Slot value = Pop(stack);
        Slot idx_val = Pop(stack);
        Slot v = Pop(stack);
        if (IsNullRef(v)) return Trap("LIST_INSERT on non-ref");
        HeapObject* obj = heap.Get(UnpackRef(v));
        if (!obj || obj->header.kind != ObjectKind::List) return Trap("LIST_INSERT on non-list");
        uint32_t length = ReadU32Payload(obj->payload, 0);
        uint32_t capacity = ReadU32Payload(obj->payload, 4);
        if (length >= capacity) return Trap("LIST_INSERT overflow");
        int32_t index = UnpackI32(idx_val);
        if (index < 0 || static_cast<uint32_t>(index) > length) return Trap("LIST_INSERT out of bounds");
        for (uint32_t i = length; i > static_cast<uint32_t>(index); --i) {
          size_t from = 8 + static_cast<size_t>(i - 1) * 4;
          size_t to = 8 + static_cast<size_t>(i) * 4;
          WriteU32Payload(obj->payload, to, ReadU32Payload(obj->payload, from));
        }
        size_t offset = 8 + static_cast<size_t>(index) * 4;
        WriteU32Payload(obj->payload, offset, UnpackRef(value));
        WriteU32Payload(obj->payload, 0, length + 1);
        break;
      }
      case OpCode::ListRemoveI32: {
        Slot idx_val = Pop(stack);
        Slot v = Pop(stack);
        if (IsNullRef(v)) return Trap("LIST_REMOVE on non-ref");
        HeapObject* obj = heap.Get(UnpackRef(v));
        if (!obj || obj->header.kind != ObjectKind::List) return Trap("LIST_REMOVE on non-list");
        uint32_t length = ReadU32Payload(obj->payload, 0);
        int32_t index = UnpackI32(idx_val);
        if (index < 0 || static_cast<uint32_t>(index) >= length) return Trap("LIST_REMOVE out of bounds");
        size_t offset = 8 + static_cast<size_t>(index) * 4;
        int32_t removed = static_cast<int32_t>(ReadU32Payload(obj->payload, offset));
        for (uint32_t i = static_cast<uint32_t>(index) + 1; i < length; ++i) {
          size_t from = 8 + static_cast<size_t>(i) * 4;
          size_t to = 8 + static_cast<size_t>(i - 1) * 4;
          WriteU32Payload(obj->payload, to, ReadU32Payload(obj->payload, from));
        }
        WriteU32Payload(obj->payload, 0, length - 1);
        Push(stack, PackI32(removed));
        break;
      }
      case OpCode::ListRemoveI64: {
        Slot idx_val = Pop(stack);
        Slot v = Pop(stack);
        if (IsNullRef(v)) return Trap("LIST_REMOVE on non-ref");
        HeapObject* obj = heap.Get(UnpackRef(v));
        if (!obj || obj->header.kind != ObjectKind::List) return Trap("LIST_REMOVE on non-list");
        uint32_t length = ReadU32Payload(obj->payload, 0);
        int32_t index = UnpackI32(idx_val);
        if (index < 0 || static_cast<uint32_t>(index) >= length) return Trap("LIST_REMOVE out of bounds");
        size_t offset = 8 + static_cast<size_t>(index) * 8;
        int64_t removed = static_cast<int64_t>(ReadU64Payload(obj->payload, offset));
        for (uint32_t i = static_cast<uint32_t>(index) + 1; i < length; ++i) {
          size_t from = 8 + static_cast<size_t>(i) * 8;
          size_t to = 8 + static_cast<size_t>(i - 1) * 8;
          WriteU64Payload(obj->payload, to, ReadU64Payload(obj->payload, from));
        }
        WriteU32Payload(obj->payload, 0, length - 1);
        Push(stack, PackI64(removed));
        break;
      }
      case OpCode::ListRemoveF32: {
        Slot idx_val = Pop(stack);
        Slot v = Pop(stack);
        if (IsNullRef(v)) return Trap("LIST_REMOVE on non-ref");
        HeapObject* obj = heap.Get(UnpackRef(v));
        if (!obj || obj->header.kind != ObjectKind::List) return Trap("LIST_REMOVE on non-list");
        uint32_t length = ReadU32Payload(obj->payload, 0);
        int32_t index = UnpackI32(idx_val);
        if (index < 0 || static_cast<uint32_t>(index) >= length) return Trap("LIST_REMOVE out of bounds");
        size_t offset = 8 + static_cast<size_t>(index) * 4;
        uint32_t removed = ReadU32Payload(obj->payload, offset);
        for (uint32_t i = static_cast<uint32_t>(index) + 1; i < length; ++i) {
          size_t from = 8 + static_cast<size_t>(i) * 4;
          size_t to = 8 + static_cast<size_t>(i - 1) * 4;
          WriteU32Payload(obj->payload, to, ReadU32Payload(obj->payload, from));
        }
        WriteU32Payload(obj->payload, 0, length - 1);
        Push(stack, PackF32Bits(removed));
        break;
      }
      case OpCode::ListRemoveF64: {
        Slot idx_val = Pop(stack);
        Slot v = Pop(stack);
        if (IsNullRef(v)) return Trap("LIST_REMOVE on non-ref");
        HeapObject* obj = heap.Get(UnpackRef(v));
        if (!obj || obj->header.kind != ObjectKind::List) return Trap("LIST_REMOVE on non-list");
        uint32_t length = ReadU32Payload(obj->payload, 0);
        int32_t index = UnpackI32(idx_val);
        if (index < 0 || static_cast<uint32_t>(index) >= length) return Trap("LIST_REMOVE out of bounds");
        size_t offset = 8 + static_cast<size_t>(index) * 8;
        uint64_t removed = ReadU64Payload(obj->payload, offset);
        for (uint32_t i = static_cast<uint32_t>(index) + 1; i < length; ++i) {
          size_t from = 8 + static_cast<size_t>(i) * 8;
          size_t to = 8 + static_cast<size_t>(i - 1) * 8;
          WriteU64Payload(obj->payload, to, ReadU64Payload(obj->payload, from));
        }
        WriteU32Payload(obj->payload, 0, length - 1);
        Push(stack, PackF64Bits(removed));
        break;
      }
      case OpCode::ListRemoveRef: {
        Slot idx_val = Pop(stack);
        Slot v = Pop(stack);
        if (IsNullRef(v)) return Trap("LIST_REMOVE on non-ref");
        HeapObject* obj = heap.Get(UnpackRef(v));
        if (!obj || obj->header.kind != ObjectKind::List) return Trap("LIST_REMOVE on non-list");
        uint32_t length = ReadU32Payload(obj->payload, 0);
        int32_t index = UnpackI32(idx_val);
        if (index < 0 || static_cast<uint32_t>(index) >= length) return Trap("LIST_REMOVE out of bounds");
        size_t offset = 8 + static_cast<size_t>(index) * 4;
        uint32_t removed = ReadU32Payload(obj->payload, offset);
        for (uint32_t i = static_cast<uint32_t>(index) + 1; i < length; ++i) {
          size_t from = 8 + static_cast<size_t>(i) * 4;
          size_t to = 8 + static_cast<size_t>(i - 1) * 4;
          WriteU32Payload(obj->payload, to, ReadU32Payload(obj->payload, from));
        }
        WriteU32Payload(obj->payload, 0, length - 1);
        Push(stack, PackRef(removed));
        break;
      }
      case OpCode::ListClear: {
        Slot v = Pop(stack);
        if (IsNullRef(v)) return Trap("LIST_CLEAR on non-ref");
        HeapObject* obj = heap.Get(UnpackRef(v));
        if (!obj || obj->header.kind != ObjectKind::List) return Trap("LIST_CLEAR on non-list");
        WriteU32Payload(obj->payload, 0, 0);
        break;
      }
      case OpCode::StringLen: {
        Slot v = Pop(stack);
        if (IsNullRef(v)) return Trap("STRING_LEN on non-ref");
        HeapObject* obj = heap.Get(UnpackRef(v));
        if (!obj || obj->header.kind != ObjectKind::String) return Trap("STRING_LEN on non-string");
        uint32_t length = ReadU32Payload(obj->payload, 0);
        Push(stack, PackI32(static_cast<int32_t>(length)));
        break;
      }
      case OpCode::StringConcat: {
        Slot b = Pop(stack);
        Slot a = Pop(stack);
        if (IsNullRef(a) || IsNullRef(b)) return Trap("STRING_CONCAT on non-ref");
        HeapObject* obj_a = heap.Get(UnpackRef(a));
        HeapObject* obj_b = heap.Get(UnpackRef(b));
        if (!obj_a || !obj_b || obj_a->header.kind != ObjectKind::String || obj_b->header.kind != ObjectKind::String) {
          return Trap("STRING_CONCAT on non-string");
        }
        std::u16string sa = ReadString(obj_a);
        std::u16string sb = ReadString(obj_b);
        std::u16string combined = sa + sb;
        uint32_t handle = CreateString(heap, combined);
        if (handle == 0xFFFFFFFFu) return Trap("STRING_CONCAT allocation failed");
        Push(stack, PackRef(handle));
        break;
      }
      case OpCode::StringGetChar: {
        Slot idx_val = Pop(stack);
        Slot v = Pop(stack);
        if (IsNullRef(v)) return Trap("STRING_GET_CHAR on non-ref");
        HeapObject* obj = heap.Get(UnpackRef(v));
        if (!obj || obj->header.kind != ObjectKind::String) return Trap("STRING_GET_CHAR on non-string");
        uint32_t length = ReadU32Payload(obj->payload, 0);
        int32_t index = UnpackI32(idx_val);
        if (index < 0 || static_cast<uint32_t>(index) >= length) return Trap("STRING_GET_CHAR out of bounds");
        size_t offset = 4 + static_cast<size_t>(index) * 2;
        uint16_t ch = ReadU16Payload(obj->payload, offset);
        Push(stack, PackI32(ch));
        break;
      }
      case OpCode::StringSlice: {
        Slot end_val = Pop(stack);
        Slot start_val = Pop(stack);
        Slot v = Pop(stack);
        if (IsNullRef(v)) return Trap("STRING_SLICE on non-ref");
        HeapObject* obj = heap.Get(UnpackRef(v));
        if (!obj || obj->header.kind != ObjectKind::String) return Trap("STRING_SLICE on non-string");
        uint32_t length = ReadU32Payload(obj->payload, 0);
        int32_t start = UnpackI32(start_val);
        int32_t end_idx = UnpackI32(end_val);
        if (start < 0 || end_idx < 0 || start > end_idx || static_cast<uint32_t>(end_idx) > length) {
          return Trap("STRING_SLICE out of bounds");
        }
        std::u16string text = ReadString(obj);
        std::u16string slice = text.substr(static_cast<size_t>(start), static_cast<size_t>(end_idx - start));
        uint32_t handle = CreateString(heap, slice);
        if (handle == 0xFFFFFFFFu) return Trap("STRING_SLICE allocation failed");
        Push(stack, PackRef(handle));
        break;
      }
      case OpCode::CallCheck: {
        if (!call_stack.empty()) return Trap("CALLCHECK not in root");
        break;
      }
      case OpCode::Line: {
        uint32_t line = ReadU32(module.code, pc);
        uint32_t column = ReadU32(module.code, pc);
        current.line = line;
        current.column = column;
        break;
      }
      case OpCode::ProfileStart: {
        ReadU32(module.code, pc);
        break;
      }
      case OpCode::ProfileEnd: {
        ReadU32(module.code, pc);
        break;
      }
      case OpCode::Intrinsic: {
        uint32_t id = ReadU32(module.code, pc);
        switch (id) {
          case kIntrinsicTrap: {
            if (stack.empty()) return Trap("INTRINSIC trap stack underflow");
            int32_t code = UnpackI32(Pop(stack));
            return Trap("INTRINSIC trap code=" + std::to_string(code));
          }
          case kIntrinsicBreakpoint:
            break;
          case kIntrinsicLogI32:
          case kIntrinsicLogI64:
          case kIntrinsicLogF32:
          case kIntrinsicLogF64:
          case kIntrinsicLogRef:
            if (stack.empty()) return Trap("INTRINSIC log stack underflow");
            Pop(stack);
            break;
          case kIntrinsicAbsI32: {
            if (stack.empty()) return Trap("INTRINSIC abs_i32 stack underflow");
            int32_t value = UnpackI32(Pop(stack));
            Push(stack, PackI32(value < 0 ? -value : value));
            break;
          }
          case kIntrinsicAbsI64: {
            if (stack.empty()) return Trap("INTRINSIC abs_i64 stack underflow");
            int64_t value = UnpackI64(Pop(stack));
            Push(stack, PackI64(value < 0 ? -value : value));
            break;
          }
          case kIntrinsicMinI32:
          case kIntrinsicMaxI32: {
            if (stack.size() < 2) return Trap("INTRINSIC min/max i32 stack underflow");
            int32_t b = UnpackI32(Pop(stack));
            int32_t a = UnpackI32(Pop(stack));
            int32_t out = (id == kIntrinsicMinI32) ? (a < b ? a : b) : (a > b ? a : b);
            Push(stack, PackI32(out));
            break;
          }
          case kIntrinsicMinI64:
          case kIntrinsicMaxI64: {
            if (stack.size() < 2) return Trap("INTRINSIC min/max i64 stack underflow");
            int64_t b = UnpackI64(Pop(stack));
            int64_t a = UnpackI64(Pop(stack));
            int64_t out = (id == kIntrinsicMinI64) ? (a < b ? a : b) : (a > b ? a : b);
            Push(stack, PackI64(out));
            break;
          }
          case kIntrinsicMinF32:
          case kIntrinsicMaxF32: {
            if (stack.size() < 2) return Trap("INTRINSIC min/max f32 stack underflow");
            float b = BitsToF32(UnpackU32Bits(Pop(stack)));
            float a = BitsToF32(UnpackU32Bits(Pop(stack)));
            float out = (id == kIntrinsicMinF32) ? (a < b ? a : b) : (a > b ? a : b);
            Push(stack, PackF32Bits(F32ToBits(out)));
            break;
          }
          case kIntrinsicMinF64:
          case kIntrinsicMaxF64: {
            if (stack.size() < 2) return Trap("INTRINSIC min/max f64 stack underflow");
            double b = BitsToF64(UnpackU64Bits(Pop(stack)));
            double a = BitsToF64(UnpackU64Bits(Pop(stack)));
            double out = (id == kIntrinsicMinF64) ? (a < b ? a : b) : (a > b ? a : b);
            Push(stack, PackF64Bits(F64ToBits(out)));
            break;
          }
          case kIntrinsicMonoNs:
          case kIntrinsicWallNs: {
            int64_t ns = 0;
            if (id == kIntrinsicMonoNs) {
              auto now = std::chrono::steady_clock::now().time_since_epoch();
              ns = static_cast<int64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(now).count());
            } else {
              auto now = std::chrono::system_clock::now().time_since_epoch();
              ns = static_cast<int64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(now).count());
            }
            Push(stack, PackI64(ns));
            break;
          }
          case kIntrinsicRandU32:
            Push(stack, PackI32(0));
            break;
          case kIntrinsicRandU64:
            Push(stack, PackI64(0));
            break;
          case kIntrinsicWriteStdout:
          case kIntrinsicWriteStderr:
            if (stack.size() < 2) return Trap("INTRINSIC write stack underflow");
            Pop(stack); // length
            Pop(stack); // ref
            break;
          case kIntrinsicPrintAny: {
            if (stack.size() < 2) return Trap("INTRINSIC print_any stack underflow");
            uint32_t tag = static_cast<uint32_t>(UnpackI32(Pop(stack)));
            Slot value = Pop(stack);
            auto write_text = [](const std::string& text) {
              if (!text.empty()) std::fwrite(text.data(), 1, text.size(), stdout);
            };
            switch (tag) {
              case kPrintAnyTagString: {
                uint32_t ref = UnpackRef(value);
                HeapObject* obj = heap.Get(ref);
                if (!obj || obj->header.kind != ObjectKind::String) {
                  return Trap("print_any: unsupported ref kind");
                }
                write_text(U16ToAscii(ReadString(obj)));
                break;
              }
              case kPrintAnyTagI8:
                write_text(std::to_string(static_cast<int32_t>(static_cast<int8_t>(UnpackI32(value)))));
                break;
              case kPrintAnyTagI16:
                write_text(std::to_string(static_cast<int32_t>(static_cast<int16_t>(UnpackI32(value)))));
                break;
              case kPrintAnyTagI32:
                write_text(std::to_string(static_cast<int32_t>(UnpackI32(value))));
                break;
              case kPrintAnyTagI64:
                write_text(std::to_string(static_cast<int64_t>(UnpackI64(value))));
                break;
              case kPrintAnyTagU8:
                write_text(std::to_string(static_cast<uint32_t>(static_cast<uint8_t>(UnpackI32(value)))));
                break;
              case kPrintAnyTagU16:
                write_text(std::to_string(static_cast<uint32_t>(static_cast<uint16_t>(UnpackI32(value)))));
                break;
              case kPrintAnyTagU32:
                write_text(std::to_string(static_cast<uint32_t>(UnpackI32(value))));
                break;
              case kPrintAnyTagU64:
                write_text(std::to_string(static_cast<uint64_t>(UnpackI64(value))));
                break;
              case kPrintAnyTagF32: {
                float v = BitsToF32(UnpackU32Bits(value));
                write_text(std::to_string(v));
                break;
              }
              case kPrintAnyTagF64: {
                double v = BitsToF64(UnpackU64Bits(value));
                write_text(std::to_string(v));
                break;
              }
              case kPrintAnyTagBool: {
                const char* text = (UnpackI32(value) != 0) ? "true" : "false";
                std::fwrite(text, 1, std::strlen(text), stdout);
                break;
              }
              case kPrintAnyTagChar: {
                uint32_t ch = static_cast<uint32_t>(UnpackI32(value)) & 0xFFu;
                char out = (ch <= 0x7Fu) ? static_cast<char>(ch) : '?';
                std::fwrite(&out, 1, 1, stdout);
                break;
              }
              default:
                return Trap("print_any: unsupported tag");
            }
            std::fflush(stdout);
            break;
          }
          case kIntrinsicDlCallI8: {
            if (stack.size() < 3) return Trap("INTRINSIC dl_call_i8 stack underflow");
            int8_t b = static_cast<int8_t>(UnpackI32(Pop(stack)));
            int8_t a = static_cast<int8_t>(UnpackI32(Pop(stack)));
            int64_t ptr_bits = UnpackI64(Pop(stack));
            if (ptr_bits == 0) return Trap("core.dl.call_i8 null ptr");
            using Fn = int8_t (*)(int8_t, int8_t);
            Fn fn = reinterpret_cast<Fn>(ptr_bits);
            Push(stack, PackI32(static_cast<int32_t>(fn(a, b))));
            break;
          }
          case kIntrinsicDlCallI16: {
            if (stack.size() < 3) return Trap("INTRINSIC dl_call_i16 stack underflow");
            int16_t b = static_cast<int16_t>(UnpackI32(Pop(stack)));
            int16_t a = static_cast<int16_t>(UnpackI32(Pop(stack)));
            int64_t ptr_bits = UnpackI64(Pop(stack));
            if (ptr_bits == 0) return Trap("core.dl.call_i16 null ptr");
            using Fn = int16_t (*)(int16_t, int16_t);
            Fn fn = reinterpret_cast<Fn>(ptr_bits);
            Push(stack, PackI32(static_cast<int32_t>(fn(a, b))));
            break;
          }
          case kIntrinsicDlCallI32: {
            if (stack.size() < 3) return Trap("INTRINSIC dl_call_i32 stack underflow");
            int32_t b = UnpackI32(Pop(stack));
            int32_t a = UnpackI32(Pop(stack));
            int64_t ptr_bits = UnpackI64(Pop(stack));
            if (ptr_bits == 0) return Trap("core.dl.call_i32 null ptr");
            using Fn = int32_t (*)(int32_t, int32_t);
            Fn fn = reinterpret_cast<Fn>(ptr_bits);
            Push(stack, PackI32(fn(a, b)));
            break;
          }
          case kIntrinsicDlCallI64: {
            if (stack.size() < 3) return Trap("INTRINSIC dl_call_i64 stack underflow");
            int64_t b = UnpackI64(Pop(stack));
            int64_t a = UnpackI64(Pop(stack));
            int64_t ptr_bits = UnpackI64(Pop(stack));
            if (ptr_bits == 0) return Trap("core.dl.call_i64 null ptr");
            using Fn = int64_t (*)(int64_t, int64_t);
            Fn fn = reinterpret_cast<Fn>(ptr_bits);
            Push(stack, PackI64(fn(a, b)));
            break;
          }
          case kIntrinsicDlCallU8: {
            if (stack.size() < 3) return Trap("INTRINSIC dl_call_u8 stack underflow");
            uint8_t b = static_cast<uint8_t>(UnpackI32(Pop(stack)));
            uint8_t a = static_cast<uint8_t>(UnpackI32(Pop(stack)));
            int64_t ptr_bits = UnpackI64(Pop(stack));
            if (ptr_bits == 0) return Trap("core.dl.call_u8 null ptr");
            using Fn = uint8_t (*)(uint8_t, uint8_t);
            Fn fn = reinterpret_cast<Fn>(ptr_bits);
            Push(stack, PackI32(static_cast<int32_t>(fn(a, b))));
            break;
          }
          case kIntrinsicDlCallU16: {
            if (stack.size() < 3) return Trap("INTRINSIC dl_call_u16 stack underflow");
            uint16_t b = static_cast<uint16_t>(UnpackI32(Pop(stack)));
            uint16_t a = static_cast<uint16_t>(UnpackI32(Pop(stack)));
            int64_t ptr_bits = UnpackI64(Pop(stack));
            if (ptr_bits == 0) return Trap("core.dl.call_u16 null ptr");
            using Fn = uint16_t (*)(uint16_t, uint16_t);
            Fn fn = reinterpret_cast<Fn>(ptr_bits);
            Push(stack, PackI32(static_cast<int32_t>(fn(a, b))));
            break;
          }
          case kIntrinsicDlCallU32: {
            if (stack.size() < 3) return Trap("INTRINSIC dl_call_u32 stack underflow");
            uint32_t b = static_cast<uint32_t>(UnpackI32(Pop(stack)));
            uint32_t a = static_cast<uint32_t>(UnpackI32(Pop(stack)));
            int64_t ptr_bits = UnpackI64(Pop(stack));
            if (ptr_bits == 0) return Trap("core.dl.call_u32 null ptr");
            using Fn = uint32_t (*)(uint32_t, uint32_t);
            Fn fn = reinterpret_cast<Fn>(ptr_bits);
            Push(stack, PackI32(static_cast<int32_t>(fn(a, b))));
            break;
          }
          case kIntrinsicDlCallU64: {
            if (stack.size() < 3) return Trap("INTRINSIC dl_call_u64 stack underflow");
            uint64_t b = static_cast<uint64_t>(UnpackI64(Pop(stack)));
            uint64_t a = static_cast<uint64_t>(UnpackI64(Pop(stack)));
            int64_t ptr_bits = UnpackI64(Pop(stack));
            if (ptr_bits == 0) return Trap("core.dl.call_u64 null ptr");
            using Fn = uint64_t (*)(uint64_t, uint64_t);
            Fn fn = reinterpret_cast<Fn>(ptr_bits);
            Push(stack, PackI64(static_cast<int64_t>(fn(a, b))));
            break;
          }
          case kIntrinsicDlCallF32: {
            if (stack.size() < 3) return Trap("INTRINSIC dl_call_f32 stack underflow");
            float b = BitsToF32(UnpackU32Bits(Pop(stack)));
            float a = BitsToF32(UnpackU32Bits(Pop(stack)));
            int64_t ptr_bits = UnpackI64(Pop(stack));
            if (ptr_bits == 0) return Trap("core.dl.call_f32 null ptr");
            using Fn = float (*)(float, float);
            Fn fn = reinterpret_cast<Fn>(ptr_bits);
            float out = fn(a, b);
            Push(stack, PackF32Bits(F32ToBits(out)));
            break;
          }
          case kIntrinsicDlCallF64: {
            if (stack.size() < 3) return Trap("INTRINSIC dl_call_f64 stack underflow");
            double b = BitsToF64(UnpackU64Bits(Pop(stack)));
            double a = BitsToF64(UnpackU64Bits(Pop(stack)));
            int64_t ptr_bits = UnpackI64(Pop(stack));
            if (ptr_bits == 0) return Trap("core.dl.call_f64 null ptr");
            using Fn = double (*)(double, double);
            Fn fn = reinterpret_cast<Fn>(ptr_bits);
            double out = fn(a, b);
            Push(stack, PackF64Bits(F64ToBits(out)));
            break;
          }
          case kIntrinsicDlCallBool: {
            if (stack.size() < 3) return Trap("INTRINSIC dl_call_bool stack underflow");
            bool b = (UnpackI32(Pop(stack)) != 0);
            bool a = (UnpackI32(Pop(stack)) != 0);
            int64_t ptr_bits = UnpackI64(Pop(stack));
            if (ptr_bits == 0) return Trap("core.dl.call_bool null ptr");
            using Fn = bool (*)(bool, bool);
            Fn fn = reinterpret_cast<Fn>(ptr_bits);
            Push(stack, PackI32(fn(a, b) ? 1 : 0));
            break;
          }
          case kIntrinsicDlCallChar: {
            if (stack.size() < 3) return Trap("INTRINSIC dl_call_char stack underflow");
            uint8_t b = static_cast<uint8_t>(UnpackI32(Pop(stack)));
            uint8_t a = static_cast<uint8_t>(UnpackI32(Pop(stack)));
            int64_t ptr_bits = UnpackI64(Pop(stack));
            if (ptr_bits == 0) return Trap("core.dl.call_char null ptr");
            using Fn = uint8_t (*)(uint8_t, uint8_t);
            Fn fn = reinterpret_cast<Fn>(ptr_bits);
            Push(stack, PackI32(static_cast<int32_t>(fn(a, b))));
            break;
          }
          case kIntrinsicDlCallStr0: {
            if (stack.empty()) return Trap("INTRINSIC dl_call_str0 stack underflow");
            int64_t ptr_bits = UnpackI64(Pop(stack));
            if (ptr_bits == 0) return Trap("core.dl.call_str0 null ptr");
            using Fn = const char* (*)();
            Fn fn = reinterpret_cast<Fn>(ptr_bits);
            const char* out = fn();
            if (!out) {
              Push(stack, PackRef(kNullRef));
              break;
            }
            uint32_t handle = CreateString(heap, AsciiToU16(out));
            Push(stack, PackRef(handle));
            break;
          }
          default:
            return Trap("INTRINSIC not supported id=" + std::to_string(id));
        }
        break;
      }
      case OpCode::SysCall: {
        uint32_t id = ReadU32(module.code, pc);
        return Trap("SYS_CALL not supported id=" + std::to_string(id));
      }
      case OpCode::AddI32:
      case OpCode::SubI32:
      case OpCode::MulI32:
      case OpCode::DivI32:
      case OpCode::ModI32: {
        Slot b = Pop(stack);
        Slot a = Pop(stack);
        int32_t lhs = UnpackI32(a);
        int32_t rhs = UnpackI32(b);
        int32_t out = 0;
        if (opcode == static_cast<uint8_t>(OpCode::AddI32)) out = lhs + rhs;
        if (opcode == static_cast<uint8_t>(OpCode::SubI32)) out = lhs - rhs;
        if (opcode == static_cast<uint8_t>(OpCode::MulI32)) out = lhs * rhs;
        if (opcode == static_cast<uint8_t>(OpCode::DivI32)) out = rhs == 0 ? 0 : (lhs / rhs);
        if (opcode == static_cast<uint8_t>(OpCode::ModI32)) out = rhs == 0 ? 0 : (lhs % rhs);
        Push(stack, PackI32(out));
        break;
      }
      case OpCode::NegI32: {
        Slot a = Pop(stack);
        int32_t out = -UnpackI32(a);
        Push(stack, PackI32(out));
        break;
      }
      case OpCode::IncI32:
      case OpCode::DecI32: {
        Slot a = Pop(stack);
        int32_t out = UnpackI32(a);
        if (opcode == static_cast<uint8_t>(OpCode::IncI32)) {
          out += 1;
        } else {
          out -= 1;
        }
        Push(stack, PackI32(out));
        break;
      }
      case OpCode::AddU32:
      case OpCode::SubU32:
      case OpCode::MulU32:
      case OpCode::DivU32:
      case OpCode::ModU32: {
        Slot b = Pop(stack);
        Slot a = Pop(stack);
        uint32_t lhs = static_cast<uint32_t>(UnpackI32(a));
        uint32_t rhs = static_cast<uint32_t>(UnpackI32(b));
        uint32_t out = 0;
        if (opcode == static_cast<uint8_t>(OpCode::AddU32)) out = lhs + rhs;
        if (opcode == static_cast<uint8_t>(OpCode::SubU32)) out = lhs - rhs;
        if (opcode == static_cast<uint8_t>(OpCode::MulU32)) out = lhs * rhs;
        if (opcode == static_cast<uint8_t>(OpCode::DivU32)) out = rhs == 0 ? 0u : (lhs / rhs);
        if (opcode == static_cast<uint8_t>(OpCode::ModU32)) out = rhs == 0 ? 0u : (lhs % rhs);
        Push(stack, PackI32(static_cast<int32_t>(out)));
        break;
      }
      case OpCode::IncU32:
      case OpCode::DecU32: {
        Slot a = Pop(stack);
        uint32_t out = static_cast<uint32_t>(UnpackI32(a));
        if (opcode == static_cast<uint8_t>(OpCode::IncU32)) {
          out += 1u;
        } else {
          out -= 1u;
        }
        Push(stack, PackI32(static_cast<int32_t>(out)));
        break;
      }
      case OpCode::IncI8:
      case OpCode::DecI8: {
        Slot a = Pop(stack);
        int8_t out = static_cast<int8_t>(UnpackI32(a));
        if (opcode == static_cast<uint8_t>(OpCode::IncI8)) {
          out = static_cast<int8_t>(out + 1);
        } else {
          out = static_cast<int8_t>(out - 1);
        }
        Push(stack, PackI32(out));
        break;
      }
      case OpCode::IncI16:
      case OpCode::DecI16: {
        Slot a = Pop(stack);
        int16_t out = static_cast<int16_t>(UnpackI32(a));
        if (opcode == static_cast<uint8_t>(OpCode::IncI16)) {
          out = static_cast<int16_t>(out + 1);
        } else {
          out = static_cast<int16_t>(out - 1);
        }
        Push(stack, PackI32(out));
        break;
      }
      case OpCode::IncU8:
      case OpCode::DecU8: {
        Slot a = Pop(stack);
        uint8_t out = static_cast<uint8_t>(UnpackI32(a));
        if (opcode == static_cast<uint8_t>(OpCode::IncU8)) {
          out = static_cast<uint8_t>(out + 1);
        } else {
          out = static_cast<uint8_t>(out - 1);
        }
        Push(stack, PackI32(static_cast<int32_t>(out)));
        break;
      }
      case OpCode::IncU16:
      case OpCode::DecU16: {
        Slot a = Pop(stack);
        uint16_t out = static_cast<uint16_t>(UnpackI32(a));
        if (opcode == static_cast<uint8_t>(OpCode::IncU16)) {
          out = static_cast<uint16_t>(out + 1);
        } else {
          out = static_cast<uint16_t>(out - 1);
        }
        Push(stack, PackI32(static_cast<int32_t>(out)));
        break;
      }
      case OpCode::NegI8: {
        Slot a = Pop(stack);
        int8_t v = static_cast<int8_t>(UnpackI32(a));
        int8_t out = static_cast<int8_t>(-v);
        Push(stack, PackI32(out));
        break;
      }
      case OpCode::NegI16: {
        Slot a = Pop(stack);
        int16_t v = static_cast<int16_t>(UnpackI32(a));
        int16_t out = static_cast<int16_t>(-v);
        Push(stack, PackI32(out));
        break;
      }
      case OpCode::NegU8: {
        Slot a = Pop(stack);
        uint8_t v = static_cast<uint8_t>(UnpackI32(a));
        uint8_t out = static_cast<uint8_t>(0u - v);
        Push(stack, PackI32(static_cast<int32_t>(out)));
        break;
      }
      case OpCode::NegU16: {
        Slot a = Pop(stack);
        uint16_t v = static_cast<uint16_t>(UnpackI32(a));
        uint16_t out = static_cast<uint16_t>(0u - v);
        Push(stack, PackI32(static_cast<int32_t>(out)));
        break;
      }
      case OpCode::NegU32: {
        Slot a = Pop(stack);
        uint32_t v = static_cast<uint32_t>(UnpackI32(a));
        uint32_t out = 0u - v;
        Push(stack, PackI32(static_cast<int32_t>(out)));
        break;
      }
      case OpCode::AndI32:
      case OpCode::OrI32:
      case OpCode::XorI32:
      case OpCode::ShlI32:
      case OpCode::ShrI32: {
        Slot b = Pop(stack);
        Slot a = Pop(stack);
        uint32_t lhs = static_cast<uint32_t>(UnpackI32(a));
        uint32_t rhs = static_cast<uint32_t>(UnpackI32(b));
        uint32_t out = 0;
        if (opcode == static_cast<uint8_t>(OpCode::AndI32)) out = lhs & rhs;
        if (opcode == static_cast<uint8_t>(OpCode::OrI32)) out = lhs | rhs;
        if (opcode == static_cast<uint8_t>(OpCode::XorI32)) out = lhs ^ rhs;
        if (opcode == static_cast<uint8_t>(OpCode::ShlI32)) out = lhs << (rhs & 31u);
        if (opcode == static_cast<uint8_t>(OpCode::ShrI32)) out = lhs >> (rhs & 31u);
        Push(stack, PackI32(static_cast<int32_t>(out)));
        break;
      }
      case OpCode::AddI64:
      case OpCode::SubI64:
      case OpCode::MulI64:
      case OpCode::DivI64:
      case OpCode::ModI64: {
        Slot b = Pop(stack);
        Slot a = Pop(stack);
        int64_t lhs = UnpackI64(a);
        int64_t rhs = UnpackI64(b);
        int64_t out = 0;
        if (opcode == static_cast<uint8_t>(OpCode::AddI64)) out = lhs + rhs;
        if (opcode == static_cast<uint8_t>(OpCode::SubI64)) out = lhs - rhs;
        if (opcode == static_cast<uint8_t>(OpCode::MulI64)) out = lhs * rhs;
        if (opcode == static_cast<uint8_t>(OpCode::DivI64)) out = rhs == 0 ? 0 : (lhs / rhs);
        if (opcode == static_cast<uint8_t>(OpCode::ModI64)) out = rhs == 0 ? 0 : (lhs % rhs);
        Push(stack, PackI64(out));
        break;
      }
      case OpCode::NegI64: {
        Slot a = Pop(stack);
        int64_t out = -UnpackI64(a);
        Push(stack, PackI64(out));
        break;
      }
      case OpCode::NegU64: {
        Slot a = Pop(stack);
        uint64_t v = static_cast<uint64_t>(UnpackI64(a));
        uint64_t out = 0u - v;
        Push(stack, PackI64(static_cast<int64_t>(out)));
        break;
      }
      case OpCode::IncI64:
      case OpCode::DecI64: {
        Slot a = Pop(stack);
        int64_t out = UnpackI64(a);
        if (opcode == static_cast<uint8_t>(OpCode::IncI64)) {
          out += 1;
        } else {
          out -= 1;
        }
        Push(stack, PackI64(out));
        break;
      }
      case OpCode::AddU64:
      case OpCode::SubU64:
      case OpCode::MulU64:
      case OpCode::DivU64:
      case OpCode::ModU64: {
        Slot b = Pop(stack);
        Slot a = Pop(stack);
        uint64_t lhs = static_cast<uint64_t>(UnpackI64(a));
        uint64_t rhs = static_cast<uint64_t>(UnpackI64(b));
        uint64_t out = 0;
        if (opcode == static_cast<uint8_t>(OpCode::AddU64)) out = lhs + rhs;
        if (opcode == static_cast<uint8_t>(OpCode::SubU64)) out = lhs - rhs;
        if (opcode == static_cast<uint8_t>(OpCode::MulU64)) out = lhs * rhs;
        if (opcode == static_cast<uint8_t>(OpCode::DivU64)) out = rhs == 0 ? 0u : (lhs / rhs);
        if (opcode == static_cast<uint8_t>(OpCode::ModU64)) out = rhs == 0 ? 0u : (lhs % rhs);
        Push(stack, PackI64(static_cast<int64_t>(out)));
        break;
      }
      case OpCode::IncU64:
      case OpCode::DecU64: {
        Slot a = Pop(stack);
        uint64_t out = static_cast<uint64_t>(UnpackI64(a));
        if (opcode == static_cast<uint8_t>(OpCode::IncU64)) {
          out += 1u;
        } else {
          out -= 1u;
        }
        Push(stack, PackI64(static_cast<int64_t>(out)));
        break;
      }
      case OpCode::AndI64:
      case OpCode::OrI64:
      case OpCode::XorI64:
      case OpCode::ShlI64:
      case OpCode::ShrI64: {
        Slot b = Pop(stack);
        Slot a = Pop(stack);
        uint64_t lhs = static_cast<uint64_t>(UnpackI64(a));
        uint64_t rhs = static_cast<uint64_t>(UnpackI64(b));
        uint64_t out = 0;
        if (opcode == static_cast<uint8_t>(OpCode::AndI64)) out = lhs & rhs;
        if (opcode == static_cast<uint8_t>(OpCode::OrI64)) out = lhs | rhs;
        if (opcode == static_cast<uint8_t>(OpCode::XorI64)) out = lhs ^ rhs;
        if (opcode == static_cast<uint8_t>(OpCode::ShlI64)) out = lhs << (rhs & 63u);
        if (opcode == static_cast<uint8_t>(OpCode::ShrI64)) out = lhs >> (rhs & 63u);
        Push(stack, PackI64(static_cast<int64_t>(out)));
        break;
      }
      case OpCode::AddF32:
      case OpCode::SubF32:
      case OpCode::MulF32:
      case OpCode::DivF32: {
        Slot b = Pop(stack);
        Slot a = Pop(stack);
        float lhs = BitsToF32(static_cast<uint32_t>(a));
        float rhs = BitsToF32(static_cast<uint32_t>(b));
        float out = 0.0f;
        if (opcode == static_cast<uint8_t>(OpCode::AddF32)) out = lhs + rhs;
        if (opcode == static_cast<uint8_t>(OpCode::SubF32)) out = lhs - rhs;
        if (opcode == static_cast<uint8_t>(OpCode::MulF32)) out = lhs * rhs;
        if (opcode == static_cast<uint8_t>(OpCode::DivF32)) out = rhs == 0.0f ? 0.0f : (lhs / rhs);
        Push(stack, PackF32Bits(F32ToBits(out)));
        break;
      }
      case OpCode::NegF32: {
        Slot a = Pop(stack);
        float out = -BitsToF32(static_cast<uint32_t>(a));
        Push(stack, PackF32Bits(F32ToBits(out)));
        break;
      }
      case OpCode::IncF32:
      case OpCode::DecF32: {
        Slot a = Pop(stack);
        float out = BitsToF32(static_cast<uint32_t>(a));
        if (opcode == static_cast<uint8_t>(OpCode::IncF32)) {
          out += 1.0f;
        } else {
          out -= 1.0f;
        }
        Push(stack, PackF32Bits(F32ToBits(out)));
        break;
      }
      case OpCode::AddF64:
      case OpCode::SubF64:
      case OpCode::MulF64:
      case OpCode::DivF64: {
        Slot b = Pop(stack);
        Slot a = Pop(stack);
        double lhs = BitsToF64(static_cast<uint64_t>(a));
        double rhs = BitsToF64(static_cast<uint64_t>(b));
        double out = 0.0;
        if (opcode == static_cast<uint8_t>(OpCode::AddF64)) out = lhs + rhs;
        if (opcode == static_cast<uint8_t>(OpCode::SubF64)) out = lhs - rhs;
        if (opcode == static_cast<uint8_t>(OpCode::MulF64)) out = lhs * rhs;
        if (opcode == static_cast<uint8_t>(OpCode::DivF64)) out = rhs == 0.0 ? 0.0 : (lhs / rhs);
        Push(stack, PackF64Bits(F64ToBits(out)));
        break;
      }
      case OpCode::NegF64: {
        Slot a = Pop(stack);
        double out = -BitsToF64(static_cast<uint64_t>(a));
        Push(stack, PackF64Bits(F64ToBits(out)));
        break;
      }
      case OpCode::IncF64:
      case OpCode::DecF64: {
        Slot a = Pop(stack);
        double out = BitsToF64(static_cast<uint64_t>(a));
        if (opcode == static_cast<uint8_t>(OpCode::IncF64)) {
          out += 1.0;
        } else {
          out -= 1.0;
        }
        Push(stack, PackF64Bits(F64ToBits(out)));
        break;
      }
      case OpCode::CmpEqI32:
      case OpCode::CmpLtI32:
      case OpCode::CmpNeI32:
      case OpCode::CmpLeI32:
      case OpCode::CmpGtI32:
      case OpCode::CmpGeI32: {
        Slot b = Pop(stack);
        Slot a = Pop(stack);
        int32_t lhs = UnpackI32(a);
        int32_t rhs = UnpackI32(b);
        bool out = false;
        if (opcode == static_cast<uint8_t>(OpCode::CmpEqI32)) out = (lhs == rhs);
        if (opcode == static_cast<uint8_t>(OpCode::CmpNeI32)) out = (lhs != rhs);
        if (opcode == static_cast<uint8_t>(OpCode::CmpLtI32)) out = (lhs < rhs);
        if (opcode == static_cast<uint8_t>(OpCode::CmpLeI32)) out = (lhs <= rhs);
        if (opcode == static_cast<uint8_t>(OpCode::CmpGtI32)) out = (lhs > rhs);
        if (opcode == static_cast<uint8_t>(OpCode::CmpGeI32)) out = (lhs >= rhs);
        Push(stack, PackI32(out ? 1 : 0));
        break;
      }
      case OpCode::CmpEqU32:
      case OpCode::CmpLtU32:
      case OpCode::CmpNeU32:
      case OpCode::CmpLeU32:
      case OpCode::CmpGtU32:
      case OpCode::CmpGeU32: {
        Slot b = Pop(stack);
        Slot a = Pop(stack);
        uint32_t lhs = static_cast<uint32_t>(UnpackI32(a));
        uint32_t rhs = static_cast<uint32_t>(UnpackI32(b));
        bool out = false;
        if (opcode == static_cast<uint8_t>(OpCode::CmpEqU32)) out = (lhs == rhs);
        if (opcode == static_cast<uint8_t>(OpCode::CmpNeU32)) out = (lhs != rhs);
        if (opcode == static_cast<uint8_t>(OpCode::CmpLtU32)) out = (lhs < rhs);
        if (opcode == static_cast<uint8_t>(OpCode::CmpLeU32)) out = (lhs <= rhs);
        if (opcode == static_cast<uint8_t>(OpCode::CmpGtU32)) out = (lhs > rhs);
        if (opcode == static_cast<uint8_t>(OpCode::CmpGeU32)) out = (lhs >= rhs);
        Push(stack, PackI32(out ? 1 : 0));
        break;
      }
      case OpCode::CmpEqI64:
      case OpCode::CmpLtI64:
      case OpCode::CmpNeI64:
      case OpCode::CmpLeI64:
      case OpCode::CmpGtI64:
      case OpCode::CmpGeI64: {
        Slot b = Pop(stack);
        Slot a = Pop(stack);
        int64_t lhs = UnpackI64(a);
        int64_t rhs = UnpackI64(b);
        bool out = false;
        if (opcode == static_cast<uint8_t>(OpCode::CmpEqI64)) out = (lhs == rhs);
        if (opcode == static_cast<uint8_t>(OpCode::CmpNeI64)) out = (lhs != rhs);
        if (opcode == static_cast<uint8_t>(OpCode::CmpLtI64)) out = (lhs < rhs);
        if (opcode == static_cast<uint8_t>(OpCode::CmpLeI64)) out = (lhs <= rhs);
        if (opcode == static_cast<uint8_t>(OpCode::CmpGtI64)) out = (lhs > rhs);
        if (opcode == static_cast<uint8_t>(OpCode::CmpGeI64)) out = (lhs >= rhs);
        Push(stack, PackI32(out ? 1 : 0));
        break;
      }
      case OpCode::CmpEqU64:
      case OpCode::CmpLtU64:
      case OpCode::CmpNeU64:
      case OpCode::CmpLeU64:
      case OpCode::CmpGtU64:
      case OpCode::CmpGeU64: {
        Slot b = Pop(stack);
        Slot a = Pop(stack);
        uint64_t lhs = static_cast<uint64_t>(UnpackI64(a));
        uint64_t rhs = static_cast<uint64_t>(UnpackI64(b));
        bool out = false;
        if (opcode == static_cast<uint8_t>(OpCode::CmpEqU64)) out = (lhs == rhs);
        if (opcode == static_cast<uint8_t>(OpCode::CmpNeU64)) out = (lhs != rhs);
        if (opcode == static_cast<uint8_t>(OpCode::CmpLtU64)) out = (lhs < rhs);
        if (opcode == static_cast<uint8_t>(OpCode::CmpLeU64)) out = (lhs <= rhs);
        if (opcode == static_cast<uint8_t>(OpCode::CmpGtU64)) out = (lhs > rhs);
        if (opcode == static_cast<uint8_t>(OpCode::CmpGeU64)) out = (lhs >= rhs);
        Push(stack, PackI32(out ? 1 : 0));
        break;
      }
      case OpCode::CmpEqF32:
      case OpCode::CmpLtF32:
      case OpCode::CmpNeF32:
      case OpCode::CmpLeF32:
      case OpCode::CmpGtF32:
      case OpCode::CmpGeF32: {
        Slot b = Pop(stack);
        Slot a = Pop(stack);
        float lhs = BitsToF32(static_cast<uint32_t>(a));
        float rhs = BitsToF32(static_cast<uint32_t>(b));
        bool out = false;
        if (opcode == static_cast<uint8_t>(OpCode::CmpEqF32)) out = (lhs == rhs);
        if (opcode == static_cast<uint8_t>(OpCode::CmpNeF32)) out = (lhs != rhs);
        if (opcode == static_cast<uint8_t>(OpCode::CmpLtF32)) out = (lhs < rhs);
        if (opcode == static_cast<uint8_t>(OpCode::CmpLeF32)) out = (lhs <= rhs);
        if (opcode == static_cast<uint8_t>(OpCode::CmpGtF32)) out = (lhs > rhs);
        if (opcode == static_cast<uint8_t>(OpCode::CmpGeF32)) out = (lhs >= rhs);
        Push(stack, PackI32(out ? 1 : 0));
        break;
      }
      case OpCode::CmpEqF64:
      case OpCode::CmpLtF64:
      case OpCode::CmpNeF64:
      case OpCode::CmpLeF64:
      case OpCode::CmpGtF64:
      case OpCode::CmpGeF64: {
        Slot b = Pop(stack);
        Slot a = Pop(stack);
        double lhs = BitsToF64(static_cast<uint64_t>(a));
        double rhs = BitsToF64(static_cast<uint64_t>(b));
        bool out = false;
        if (opcode == static_cast<uint8_t>(OpCode::CmpEqF64)) out = (lhs == rhs);
        if (opcode == static_cast<uint8_t>(OpCode::CmpNeF64)) out = (lhs != rhs);
        if (opcode == static_cast<uint8_t>(OpCode::CmpLtF64)) out = (lhs < rhs);
        if (opcode == static_cast<uint8_t>(OpCode::CmpLeF64)) out = (lhs <= rhs);
        if (opcode == static_cast<uint8_t>(OpCode::CmpGtF64)) out = (lhs > rhs);
        if (opcode == static_cast<uint8_t>(OpCode::CmpGeF64)) out = (lhs >= rhs);
        Push(stack, PackI32(out ? 1 : 0));
        break;
      }
      case OpCode::BoolNot: {
        Slot v = Pop(stack);
        Push(stack, PackI32(UnpackI32(v) ? 0 : 1));
        break;
      }
      case OpCode::BoolAnd:
      case OpCode::BoolOr: {
        Slot b = Pop(stack);
        Slot a = Pop(stack);
        bool out = (opcode == static_cast<uint8_t>(OpCode::BoolAnd)) ?
            (UnpackI32(a) != 0 && UnpackI32(b) != 0) :
            (UnpackI32(a) != 0 || UnpackI32(b) != 0);
        Push(stack, PackI32(out ? 1 : 0));
        break;
      }
      case OpCode::Jmp: {
        int32_t rel = ReadI32(module.code, pc);
        pc = static_cast<size_t>(static_cast<int64_t>(pc) + rel);
        if (pc < func_start || pc > end) return Trap("JMP out of bounds");
        break;
      }
      case OpCode::JmpTable: {
        uint32_t const_id = ReadU32(module.code, pc);
        int32_t default_rel = ReadI32(module.code, pc);
        Slot index = Pop(stack);
        if (const_id + 8 > module.const_pool.size()) return Trap("JMP_TABLE const id bad");
        uint32_t kind = ReadU32Payload(module.const_pool, const_id);
        if (kind != 6) return Trap("JMP_TABLE const kind mismatch");
        uint32_t payload = ReadU32Payload(module.const_pool, const_id + 4);
        if (payload + 4 > module.const_pool.size()) return Trap("JMP_TABLE blob out of bounds");
        uint32_t blob_len = ReadU32Payload(module.const_pool, payload);
        if (payload + 4 + blob_len > module.const_pool.size()) return Trap("JMP_TABLE blob out of bounds");
        if (blob_len < 4 || (blob_len - 4) % 4 != 0) return Trap("JMP_TABLE blob size invalid");
        uint32_t count = ReadU32Payload(module.const_pool, payload + 4);
        if (blob_len != 4 + count * 4) return Trap("JMP_TABLE blob size mismatch");
        int32_t rel = default_rel;
        int32_t idx_val = UnpackI32(index);
        if (idx_val >= 0 && static_cast<uint32_t>(idx_val) < count) {
          size_t off_pos = payload + 8 + static_cast<size_t>(idx_val) * 4u;
          uint32_t raw = ReadU32Payload(module.const_pool, off_pos);
          rel = static_cast<int32_t>(raw);
        }
        pc = static_cast<size_t>(static_cast<int64_t>(pc) + rel);
        if (pc < func_start || pc > end) return Trap("JMP_TABLE out of bounds");
        break;
      }
      case OpCode::JmpTrue:
      case OpCode::JmpFalse: {
        int32_t rel = ReadI32(module.code, pc);
        Slot cond = Pop(stack);
        bool take = UnpackI32(cond) != 0;
        if (opcode == static_cast<uint8_t>(OpCode::JmpFalse)) take = !take;
        if (take) {
          pc = static_cast<size_t>(static_cast<int64_t>(pc) + rel);
          if (pc < func_start || pc > end) return Trap("JMP out of bounds");
        }
        break;
      }
      case OpCode::Enter: {
        uint16_t locals = ReadU16(module.code, pc);
        if (locals != current.locals_count) return Trap("ENTER local count mismatch");
        break;
      }
      case OpCode::Leave:
        break;
      case OpCode::Call: {
        uint32_t func_id = ReadU32(module.code, pc);
        uint8_t arg_count = ReadU8(module.code, pc);
        if (func_id >= module.functions.size()) return Trap("CALL invalid function id");
        const auto& func = module.functions[func_id];
        if (func.method_id >= module.methods.size()) return Trap("CALL invalid method id");
        const auto& method = module.methods[func.method_id];
        if (method.sig_id >= module.sigs.size()) return Trap("CALL invalid signature id");
        const auto& sig = module.sigs[method.sig_id];
        if (arg_count != sig.param_count) return Trap("CALL arg count mismatch");
        if (stack.size() < arg_count) return Trap("CALL stack underflow");

        call_args.resize(arg_count);
        for (int i = static_cast<int>(arg_count) - 1; i >= 0; --i) {
          call_args[static_cast<size_t>(i)] = Pop(stack);
        }
        if (func_id < module.function_is_import.size() && module.function_is_import[func_id]) {
          Slot ret = 0;
          bool has_ret = false;
          std::string error;
          if (!handle_import_call(func_id, call_args, ret, has_ret, error)) {
            return Trap(error);
          }
          if (has_ret) Push(stack, ret);
          break;
        }
        if (enable_jit && jit_stubs[func_id].active) {
          // JIT stub placeholder: still runs interpreter path.
          jit_dispatch_counts[func_id] += 1;
        }

        if (enable_jit && jit_stubs[func_id].compiled) {
          update_tier(func_id);
          jit_compiled_exec_counts[func_id] += 1;
          if (jit_tiers[func_id] == JitTier::Tier1) {
            jit_tier1_exec_counts[func_id] += 1;
          }
          Slot ret = 0;
          bool has_ret = false;
          std::string error;
          if (run_compiled(func_id, ret, has_ret, error)) {
            if (has_ret) Push(stack, ret);
            break;
          }
          jit_stubs[func_id].compiled = false;
          jit_stubs[func_id].disabled = true;
        }

        current.return_pc = pc;
        current.stack_base = stack.size();
        call_stack.push_back(current);
        current = setup_frame(func_id, pc, stack.size(), kNullRef);
        for (size_t i = 0; i < call_args.size() && i < current.locals_count; ++i) {
          locals_arena[current.locals_base + i] = call_args[i];
        }
        func_start = func.code_offset;
        pc = func_start;
        end = func_start + func.code_size;
        break;
      }
      case OpCode::CallIndirect: {
        uint32_t sig_id = ReadU32(module.code, pc);
        uint8_t arg_count = ReadU8(module.code, pc);
        if (sig_id >= module.sigs.size()) return Trap("CALL_INDIRECT invalid signature id");
        const auto& sig = module.sigs[sig_id];
        if (arg_count != sig.param_count) return Trap("CALL_INDIRECT arg count mismatch");
        if (stack.size() < static_cast<size_t>(arg_count) + 1u) return Trap("CALL_INDIRECT stack underflow");
        Slot func_val = Pop(stack);
        int64_t func_index = -1;
        uint32_t closure_ref = kNullRef;
        uint32_t handle = UnpackRef(func_val);
        if (handle != kNullRef) {
          HeapObject* obj = heap.Get(handle);
          if (obj && obj->header.kind == ObjectKind::Closure) {
            uint32_t method_id = ReadU32Payload(obj->payload, 0);
            bool found = false;
            for (size_t i = 0; i < module.functions.size(); ++i) {
              if (module.functions[i].method_id == method_id) {
                func_index = static_cast<int64_t>(i);
                found = true;
                break;
              }
            }
            if (!found) return Trap("CALL_INDIRECT closure method not found");
            closure_ref = handle;
          }
        }
        if (func_index < 0) {
          int32_t idx = UnpackI32(func_val);
          if (idx < 0 || static_cast<size_t>(idx) >= module.functions.size()) {
            return Trap("CALL_INDIRECT invalid function id");
          }
          func_index = idx;
        }

        call_args.resize(arg_count);
        for (int i = static_cast<int>(arg_count) - 1; i >= 0; --i) {
          call_args[static_cast<size_t>(i)] = Pop(stack);
        }
        if (static_cast<size_t>(func_index) < module.function_is_import.size() &&
            module.function_is_import[static_cast<size_t>(func_index)]) {
          if (closure_ref != kNullRef) {
            return Trap("CALL_INDIRECT import closure unsupported");
          }
          Slot ret = 0;
          bool has_ret = false;
          std::string error;
          if (!handle_import_call(static_cast<uint32_t>(func_index), call_args, ret, has_ret, error)) {
            return Trap(error);
          }
          if (has_ret) Push(stack, ret);
          break;
        }

        if (enable_jit && jit_stubs[static_cast<size_t>(func_index)].active) {
          // JIT stub placeholder: still runs interpreter path.
          jit_dispatch_counts[static_cast<size_t>(func_index)] += 1;
        }

        if (enable_jit && jit_stubs[static_cast<size_t>(func_index)].compiled) {
          update_tier(static_cast<size_t>(func_index));
          jit_compiled_exec_counts[static_cast<size_t>(func_index)] += 1;
          if (jit_tiers[static_cast<size_t>(func_index)] == JitTier::Tier1) {
            jit_tier1_exec_counts[static_cast<size_t>(func_index)] += 1;
          }
          Slot ret = 0;
          bool has_ret = false;
          std::string error;
          if (run_compiled(static_cast<size_t>(func_index), ret, has_ret, error)) {
            if (has_ret) Push(stack, ret);
            break;
          }
          jit_stubs[static_cast<size_t>(func_index)].compiled = false;
          jit_stubs[static_cast<size_t>(func_index)].disabled = true;
        }

        current.return_pc = pc;
        current.stack_base = stack.size();
        call_stack.push_back(current);
        current = setup_frame(static_cast<size_t>(func_index), pc, stack.size(), closure_ref);
        for (size_t i = 0; i < call_args.size() && i < current.locals_count; ++i) {
          locals_arena[current.locals_base + i] = call_args[i];
        }
        const auto& func = module.functions[static_cast<size_t>(func_index)];
        func_start = func.code_offset;
        pc = func_start;
        end = func_start + func.code_size;
        break;
      }
      case OpCode::TailCall: {
        uint32_t func_id = ReadU32(module.code, pc);
        uint8_t arg_count = ReadU8(module.code, pc);
        if (func_id >= module.functions.size()) return Trap("TAILCALL invalid function id");
        if (enable_jit && jit_stubs[func_id].active) {
          // JIT stub placeholder: still runs interpreter path.
          jit_dispatch_counts[func_id] += 1;
        }
        const auto& func = module.functions[func_id];
        if (func.method_id >= module.methods.size()) return Trap("TAILCALL invalid method id");
        const auto& method = module.methods[func.method_id];
        if (method.sig_id >= module.sigs.size()) return Trap("TAILCALL invalid signature id");
        const auto& sig = module.sigs[method.sig_id];
        if (arg_count != sig.param_count) return Trap("TAILCALL arg count mismatch");
        if (stack.size() < arg_count) return Trap("TAILCALL stack underflow");

        call_args.resize(arg_count);
        for (int i = static_cast<int>(arg_count) - 1; i >= 0; --i) {
          call_args[static_cast<size_t>(i)] = Pop(stack);
        }
        if (func_id < module.function_is_import.size() && module.function_is_import[func_id]) {
          Slot ret = 0;
          bool has_ret = false;
          std::string error;
          if (!handle_import_call(func_id, call_args, ret, has_ret, error)) {
            return Trap(error);
          }
          if (call_stack.empty()) {
            ExecResult result;
            result.status = ExecStatus::Halted;
            if (has_ret) result.exit_code = UnpackI32(ret);
            return finish(result);
          }
          Frame caller = call_stack.back();
          call_stack.pop_back();
          stack.resize(caller.stack_base);
          locals_arena.resize(caller.locals_base + caller.locals_count);
          if (has_ret) Push(stack, ret);
          current = caller;
          pc = current.return_pc;
          const auto& func = module.functions[current.func_index];
          func_start = func.code_offset;
          end = func_start + func.code_size;
          break;
        }

        if (enable_jit && jit_stubs[func_id].compiled) {
          update_tier(func_id);
          jit_compiled_exec_counts[func_id] += 1;
          if (jit_tiers[func_id] == JitTier::Tier1) {
            jit_tier1_exec_counts[func_id] += 1;
          }
          Slot ret = 0;
          bool has_ret = false;
          std::string error;
          if (run_compiled(func_id, ret, has_ret, error)) {
            if (call_stack.empty()) {
              ExecResult result;
              result.status = ExecStatus::Halted;
              if (has_ret) result.exit_code = UnpackI32(ret);
              return finish(result);
            }
            Frame caller = call_stack.back();
            call_stack.pop_back();
            stack.resize(caller.stack_base);
            locals_arena.resize(caller.locals_base + caller.locals_count);
            if (has_ret) Push(stack, ret);
            current = caller;
            pc = current.return_pc;
            const auto& func = module.functions[current.func_index];
            func_start = func.code_offset;
            end = func_start + func.code_size;
            break;
          }
          jit_stubs[func_id].compiled = false;
          jit_stubs[func_id].disabled = true;
        }

        size_t return_pc = current.return_pc;
        size_t stack_base = current.stack_base;
        locals_arena.resize(current.locals_base);
        stack.resize(stack_base);
        current = setup_frame(func_id, return_pc, stack_base, kNullRef);
        for (size_t i = 0; i < call_args.size() && i < current.locals_count; ++i) {
          locals_arena[current.locals_base + i] = call_args[i];
        }
        func_start = func.code_offset;
        pc = func_start;
        end = func_start + func.code_size;
        break;
      }
      case OpCode::ConvI32ToI64: {
        Slot v = Pop(stack);
        Push(stack, PackI64(static_cast<int64_t>(UnpackI32(v))));
        break;
      }
      case OpCode::ConvI64ToI32: {
        Slot v = Pop(stack);
        Push(stack, PackI32(static_cast<int32_t>(UnpackI64(v))));
        break;
      }
      case OpCode::ConvI32ToF32: {
        Slot v = Pop(stack);
        float out = static_cast<float>(UnpackI32(v));
        Push(stack, PackF32Bits(F32ToBits(out)));
        break;
      }
      case OpCode::ConvI32ToF64: {
        Slot v = Pop(stack);
        double out = static_cast<double>(UnpackI32(v));
        Push(stack, PackF64Bits(F64ToBits(out)));
        break;
      }
      case OpCode::ConvF32ToI32: {
        Slot v = Pop(stack);
        float in = BitsToF32(static_cast<uint32_t>(v));
        Push(stack, PackI32(static_cast<int32_t>(in)));
        break;
      }
      case OpCode::ConvF64ToI32: {
        Slot v = Pop(stack);
        double in = BitsToF64(static_cast<uint64_t>(v));
        Push(stack, PackI32(static_cast<int32_t>(in)));
        break;
      }
      case OpCode::ConvF32ToF64: {
        Slot v = Pop(stack);
        double out = static_cast<double>(BitsToF32(static_cast<uint32_t>(v)));
        Push(stack, PackF64Bits(F64ToBits(out)));
        break;
      }
      case OpCode::ConvF64ToF32: {
        Slot v = Pop(stack);
        float out = static_cast<float>(BitsToF64(static_cast<uint64_t>(v)));
        Push(stack, PackF32Bits(F32ToBits(out)));
        break;
      }
      case OpCode::Ret: {
        Slot ret = 0;
        bool has_ret = false;
        if (!stack.empty()) {
          ret = Pop(stack);
          has_ret = true;
        }
        if (call_stack.empty()) {
          ExecResult result;
          result.status = ExecStatus::Halted;
          if (has_ret) result.exit_code = UnpackI32(ret);
          return finish(result);
        }
        Frame caller = call_stack.back();
        call_stack.pop_back();
        stack.resize(caller.stack_base);
        locals_arena.resize(caller.locals_base + caller.locals_count);
        if (has_ret) Push(stack, ret);
        current = caller;
        pc = current.return_pc;
        const auto& func = module.functions[current.func_index];
        func_start = func.code_offset;
        end = func_start + func.code_size;
        break;
      }
      default:
        return Trap("unsupported opcode");
    }
  }

  ExecResult result;
  result.status = ExecStatus::Halted;
  return finish(result);
}

} // namespace Simple::VM
