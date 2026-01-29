#include "vm.h"

#include <cstdint>
#include <vector>

#include "heap.h"
#include "opcode.h"
#include "sbc_verifier.h"

namespace simplevm {
namespace {

enum class ValueKind {
  I32,
  Bool,
  Ref,
  None,
};

struct Value {
  ValueKind kind = ValueKind::None;
  int64_t i64 = 0;
};

struct Frame {
  size_t func_index = 0;
  size_t return_pc = 0;
  size_t stack_base = 0;
  std::vector<Value> locals;
};

int32_t ReadI32(const std::vector<uint8_t>& code, size_t& pc) {
  uint32_t v = static_cast<uint32_t>(code[pc]) |
               (static_cast<uint32_t>(code[pc + 1]) << 8) |
               (static_cast<uint32_t>(code[pc + 2]) << 16) |
               (static_cast<uint32_t>(code[pc + 3]) << 24);
  pc += 4;
  return static_cast<int32_t>(v);
}

uint32_t ReadU32(const std::vector<uint8_t>& code, size_t& pc) {
  uint32_t v = static_cast<uint32_t>(code[pc]) |
               (static_cast<uint32_t>(code[pc + 1]) << 8) |
               (static_cast<uint32_t>(code[pc + 2]) << 16) |
               (static_cast<uint32_t>(code[pc + 3]) << 24);
  pc += 4;
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

Value Pop(std::vector<Value>& stack) {
  Value v = stack.back();
  stack.pop_back();
  return v;
}

void Push(std::vector<Value>& stack, Value v) {
  stack.push_back(v);
}

uint32_t ReadU32Payload(const std::vector<uint8_t>& payload, size_t offset) {
  return static_cast<uint32_t>(payload[offset]) |
         (static_cast<uint32_t>(payload[offset + 1]) << 8) |
         (static_cast<uint32_t>(payload[offset + 2]) << 16) |
         (static_cast<uint32_t>(payload[offset + 3]) << 24);
}

void WriteU32Payload(std::vector<uint8_t>& payload, size_t offset, uint32_t value) {
  payload[offset + 0] = static_cast<uint8_t>(value & 0xFF);
  payload[offset + 1] = static_cast<uint8_t>((value >> 8) & 0xFF);
  payload[offset + 2] = static_cast<uint8_t>((value >> 16) & 0xFF);
  payload[offset + 3] = static_cast<uint8_t>((value >> 24) & 0xFF);
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
  result.error = message;
  return result;
}

} // namespace

ExecResult ExecuteModule(const SbcModule& module) {
  return ExecuteModule(module, true);
}

ExecResult ExecuteModule(const SbcModule& module, bool verify) {
  if (verify) {
    VerifyResult vr = VerifyModule(module);
    if (!vr.ok) return Trap(vr.error);
  }
  if (module.functions.empty()) return Trap("no functions to execute");
  if (module.header.entry_method_id == 0xFFFFFFFFu) return Trap("no entry point");

  Heap heap;
  std::vector<Value> globals(module.globals.size());

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

  std::vector<Value> stack;
  std::vector<Frame> call_stack;

  auto setup_frame = [&](size_t func_index, size_t return_pc, size_t stack_base) -> Frame {
    Frame frame;
    frame.func_index = func_index;
    frame.return_pc = return_pc;
    frame.stack_base = stack_base;
    uint32_t method_id = module.functions[func_index].method_id;
    if (method_id >= module.methods.size()) {
      frame.locals.clear();
      return frame;
    }
    uint16_t local_count = module.methods[method_id].local_count;
    frame.locals.resize(local_count);
    return frame;
  };

  Frame current = setup_frame(entry_func_index, 0, 0);
  size_t func_start = module.functions[entry_func_index].code_offset;
  size_t pc = func_start;
  size_t end = func_start + module.functions[entry_func_index].code_size;

  size_t op_counter = 0;
  auto maybe_collect = [&]() {
    if (op_counter % 1000 != 0) return;
    heap.ResetMarks();
    for (const auto& g : globals) {
      if (g.kind == ValueKind::Ref && g.i64 >= 0) heap.Mark(static_cast<uint32_t>(g.i64));
    }
    for (const auto& v : stack) {
      if (v.kind == ValueKind::Ref && v.i64 >= 0) heap.Mark(static_cast<uint32_t>(v.i64));
    }
    for (const auto& f : call_stack) {
      for (const auto& v : f.locals) {
        if (v.kind == ValueKind::Ref && v.i64 >= 0) heap.Mark(static_cast<uint32_t>(v.i64));
      }
    }
    for (const auto& v : current.locals) {
      if (v.kind == ValueKind::Ref && v.i64 >= 0) heap.Mark(static_cast<uint32_t>(v.i64));
    }
    heap.Sweep();
  };

  while (pc < module.code.size()) {
    ++op_counter;
    maybe_collect();
    if (pc >= end) {
      if (call_stack.empty()) {
        ExecResult done;
        done.status = ExecStatus::Halted;
        return done;
      }
      return Trap("pc out of bounds for function");
    }

    uint8_t opcode = module.code[pc++];
    switch (static_cast<OpCode>(opcode)) {
      case OpCode::Nop:
        break;
      case OpCode::Halt: {
        ExecResult result;
        result.status = ExecStatus::Halted;
        if (!stack.empty() && stack.back().kind == ValueKind::I32) {
          result.exit_code = static_cast<int32_t>(stack.back().i64);
        }
        return result;
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
        Value b = stack[stack.size() - 1];
        Value a = stack[stack.size() - 2];
        stack.push_back(a);
        stack.push_back(b);
        break;
      }
      case OpCode::Swap: {
        if (stack.size() < 2) return Trap("SWAP on short stack");
        Value a = stack[stack.size() - 1];
        Value b = stack[stack.size() - 2];
        stack[stack.size() - 1] = b;
        stack[stack.size() - 2] = a;
        break;
      }
      case OpCode::Rot: {
        if (stack.size() < 3) return Trap("ROT on short stack");
        Value c = stack[stack.size() - 1];
        Value b = stack[stack.size() - 2];
        Value a = stack[stack.size() - 3];
        stack[stack.size() - 3] = b;
        stack[stack.size() - 2] = c;
        stack[stack.size() - 1] = a;
        break;
      }
      case OpCode::ConstI32: {
        int32_t value = ReadI32(module.code, pc);
        Push(stack, Value{ValueKind::I32, value});
        break;
      }
      case OpCode::ConstU32: {
        uint32_t value = ReadU32(module.code, pc);
        Push(stack, Value{ValueKind::I32, static_cast<int32_t>(value)});
        break;
      }
      case OpCode::ConstI8: {
        int8_t value = static_cast<int8_t>(ReadU8(module.code, pc));
        Push(stack, Value{ValueKind::I32, value});
        break;
      }
      case OpCode::ConstI16: {
        int16_t value = static_cast<int16_t>(ReadU16(module.code, pc));
        Push(stack, Value{ValueKind::I32, value});
        break;
      }
      case OpCode::ConstU8: {
        uint8_t value = ReadU8(module.code, pc);
        Push(stack, Value{ValueKind::I32, value});
        break;
      }
      case OpCode::ConstU16: {
        uint16_t value = ReadU16(module.code, pc);
        Push(stack, Value{ValueKind::I32, value});
        break;
      }
      case OpCode::ConstChar: {
        uint16_t value = ReadU16(module.code, pc);
        Push(stack, Value{ValueKind::I32, value});
        break;
      }
      case OpCode::ConstBool: {
        uint8_t v = ReadU8(module.code, pc);
        Push(stack, Value{ValueKind::Bool, v ? 1 : 0});
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
        Push(stack, Value{ValueKind::Ref, static_cast<int64_t>(handle)});
        break;
      }
      case OpCode::ConstNull: {
        Push(stack, Value{ValueKind::Ref, -1});
        break;
      }
      case OpCode::LoadLocal: {
        uint32_t idx = ReadU32(module.code, pc);
        if (idx >= current.locals.size()) return Trap("LOAD_LOCAL out of range");
        Push(stack, current.locals[idx]);
        break;
      }
      case OpCode::StoreLocal: {
        uint32_t idx = ReadU32(module.code, pc);
        if (idx >= current.locals.size()) return Trap("STORE_LOCAL out of range");
        current.locals[idx] = Pop(stack);
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
      case OpCode::NewObject: {
        uint32_t type_id = ReadU32(module.code, pc);
        if (type_id >= module.types.size()) return Trap("NEW_OBJECT bad type id");
        uint32_t size = module.types[type_id].size;
        uint32_t handle = heap.Allocate(ObjectKind::Artifact, type_id, size);
        Push(stack, Value{ValueKind::Ref, static_cast<int64_t>(handle)});
        break;
      }
      case OpCode::LoadField: {
        uint32_t field_id = ReadU32(module.code, pc);
        Value v = Pop(stack);
        if (v.kind != ValueKind::Ref || v.i64 < 0) return Trap("LOAD_FIELD on non-ref");
        if (field_id >= module.fields.size()) return Trap("LOAD_FIELD bad field id");
        HeapObject* obj = heap.Get(static_cast<uint32_t>(v.i64));
        if (!obj || obj->header.kind != ObjectKind::Artifact) return Trap("LOAD_FIELD on non-object");
        uint32_t offset = module.fields[field_id].offset;
        if (offset + 4 > obj->payload.size()) return Trap("LOAD_FIELD out of bounds");
        int32_t value = static_cast<int32_t>(ReadU32Payload(obj->payload, offset));
        Push(stack, Value{ValueKind::I32, value});
        break;
      }
      case OpCode::StoreField: {
        uint32_t field_id = ReadU32(module.code, pc);
        Value value = Pop(stack);
        Value v = Pop(stack);
        if (v.kind != ValueKind::Ref || v.i64 < 0) return Trap("STORE_FIELD on non-ref");
        if (value.kind != ValueKind::I32) return Trap("STORE_FIELD type mismatch");
        if (field_id >= module.fields.size()) return Trap("STORE_FIELD bad field id");
        HeapObject* obj = heap.Get(static_cast<uint32_t>(v.i64));
        if (!obj || obj->header.kind != ObjectKind::Artifact) return Trap("STORE_FIELD on non-object");
        uint32_t offset = module.fields[field_id].offset;
        if (offset + 4 > obj->payload.size()) return Trap("STORE_FIELD out of bounds");
        WriteU32Payload(obj->payload, offset, static_cast<uint32_t>(value.i64));
        break;
      }
      case OpCode::IsNull: {
        Value v = Pop(stack);
        if (v.kind != ValueKind::Ref) return Trap("IS_NULL on non-ref");
        Push(stack, Value{ValueKind::Bool, v.i64 < 0 ? 1 : 0});
        break;
      }
      case OpCode::RefEq:
      case OpCode::RefNe: {
        Value b = Pop(stack);
        Value a = Pop(stack);
        if (a.kind != ValueKind::Ref || b.kind != ValueKind::Ref) return Trap("REF_EQ/REF_NE on non-ref");
        bool out = (a.i64 == b.i64);
        if (opcode == static_cast<uint8_t>(OpCode::RefNe)) out = !out;
        Push(stack, Value{ValueKind::Bool, out ? 1 : 0});
        break;
      }
      case OpCode::TypeOf: {
        Value v = Pop(stack);
        if (v.kind != ValueKind::Ref || v.i64 < 0) return Trap("TYPEOF on non-ref");
        HeapObject* obj = heap.Get(static_cast<uint32_t>(v.i64));
        if (!obj) return Trap("TYPEOF on invalid ref");
        Push(stack, Value{ValueKind::I32, static_cast<int32_t>(obj->header.type_id)});
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
        Push(stack, Value{ValueKind::Ref, static_cast<int64_t>(handle)});
        break;
      }
      case OpCode::ArrayLen: {
        Value v = Pop(stack);
        if (v.kind != ValueKind::Ref || v.i64 < 0) return Trap("ARRAY_LEN on non-ref");
        HeapObject* obj = heap.Get(static_cast<uint32_t>(v.i64));
        if (!obj || obj->header.kind != ObjectKind::Array) return Trap("ARRAY_LEN on non-array");
        uint32_t length = ReadU32Payload(obj->payload, 0);
        Push(stack, Value{ValueKind::I32, static_cast<int32_t>(length)});
        break;
      }
      case OpCode::ArrayGetI32: {
        Value idx = Pop(stack);
        Value v = Pop(stack);
        if (v.kind != ValueKind::Ref || v.i64 < 0) return Trap("ARRAY_GET on non-ref");
        if (idx.kind != ValueKind::I32) return Trap("ARRAY_GET index not i32");
        HeapObject* obj = heap.Get(static_cast<uint32_t>(v.i64));
        if (!obj || obj->header.kind != ObjectKind::Array) return Trap("ARRAY_GET on non-array");
        uint32_t length = ReadU32Payload(obj->payload, 0);
        int32_t index = static_cast<int32_t>(idx.i64);
        if (index < 0 || static_cast<uint32_t>(index) >= length) return Trap("ARRAY_GET out of bounds");
        size_t offset = 4 + static_cast<size_t>(index) * 4;
        int32_t value = static_cast<int32_t>(ReadU32Payload(obj->payload, offset));
        Push(stack, Value{ValueKind::I32, value});
        break;
      }
      case OpCode::ArraySetI32: {
        Value value = Pop(stack);
        Value idx = Pop(stack);
        Value v = Pop(stack);
        if (v.kind != ValueKind::Ref || v.i64 < 0) return Trap("ARRAY_SET on non-ref");
        if (idx.kind != ValueKind::I32 || value.kind != ValueKind::I32) return Trap("ARRAY_SET type mismatch");
        HeapObject* obj = heap.Get(static_cast<uint32_t>(v.i64));
        if (!obj || obj->header.kind != ObjectKind::Array) return Trap("ARRAY_SET on non-array");
        uint32_t length = ReadU32Payload(obj->payload, 0);
        int32_t index = static_cast<int32_t>(idx.i64);
        if (index < 0 || static_cast<uint32_t>(index) >= length) return Trap("ARRAY_SET out of bounds");
        size_t offset = 4 + static_cast<size_t>(index) * 4;
        WriteU32Payload(obj->payload, offset, static_cast<uint32_t>(value.i64));
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
        Push(stack, Value{ValueKind::Ref, static_cast<int64_t>(handle)});
        break;
      }
      case OpCode::ListLen: {
        Value v = Pop(stack);
        if (v.kind != ValueKind::Ref || v.i64 < 0) return Trap("LIST_LEN on non-ref");
        HeapObject* obj = heap.Get(static_cast<uint32_t>(v.i64));
        if (!obj || obj->header.kind != ObjectKind::List) return Trap("LIST_LEN on non-list");
        uint32_t length = ReadU32Payload(obj->payload, 0);
        Push(stack, Value{ValueKind::I32, static_cast<int32_t>(length)});
        break;
      }
      case OpCode::ListGetI32: {
        Value idx = Pop(stack);
        Value v = Pop(stack);
        if (v.kind != ValueKind::Ref || v.i64 < 0) return Trap("LIST_GET on non-ref");
        if (idx.kind != ValueKind::I32) return Trap("LIST_GET index not i32");
        HeapObject* obj = heap.Get(static_cast<uint32_t>(v.i64));
        if (!obj || obj->header.kind != ObjectKind::List) return Trap("LIST_GET on non-list");
        uint32_t length = ReadU32Payload(obj->payload, 0);
        int32_t index = static_cast<int32_t>(idx.i64);
        if (index < 0 || static_cast<uint32_t>(index) >= length) return Trap("LIST_GET out of bounds");
        size_t offset = 8 + static_cast<size_t>(index) * 4;
        int32_t value = static_cast<int32_t>(ReadU32Payload(obj->payload, offset));
        Push(stack, Value{ValueKind::I32, value});
        break;
      }
      case OpCode::ListSetI32: {
        Value value = Pop(stack);
        Value idx = Pop(stack);
        Value v = Pop(stack);
        if (v.kind != ValueKind::Ref || v.i64 < 0) return Trap("LIST_SET on non-ref");
        if (idx.kind != ValueKind::I32 || value.kind != ValueKind::I32) return Trap("LIST_SET type mismatch");
        HeapObject* obj = heap.Get(static_cast<uint32_t>(v.i64));
        if (!obj || obj->header.kind != ObjectKind::List) return Trap("LIST_SET on non-list");
        uint32_t length = ReadU32Payload(obj->payload, 0);
        int32_t index = static_cast<int32_t>(idx.i64);
        if (index < 0 || static_cast<uint32_t>(index) >= length) return Trap("LIST_SET out of bounds");
        size_t offset = 8 + static_cast<size_t>(index) * 4;
        WriteU32Payload(obj->payload, offset, static_cast<uint32_t>(value.i64));
        break;
      }
      case OpCode::ListPushI32: {
        Value value = Pop(stack);
        Value v = Pop(stack);
        if (v.kind != ValueKind::Ref || v.i64 < 0) return Trap("LIST_PUSH on non-ref");
        if (value.kind != ValueKind::I32) return Trap("LIST_PUSH type mismatch");
        HeapObject* obj = heap.Get(static_cast<uint32_t>(v.i64));
        if (!obj || obj->header.kind != ObjectKind::List) return Trap("LIST_PUSH on non-list");
        uint32_t length = ReadU32Payload(obj->payload, 0);
        uint32_t capacity = ReadU32Payload(obj->payload, 4);
        if (length >= capacity) return Trap("LIST_PUSH overflow");
        size_t offset = 8 + static_cast<size_t>(length) * 4;
        WriteU32Payload(obj->payload, offset, static_cast<uint32_t>(value.i64));
        WriteU32Payload(obj->payload, 0, length + 1);
        break;
      }
      case OpCode::ListPopI32: {
        Value v = Pop(stack);
        if (v.kind != ValueKind::Ref || v.i64 < 0) return Trap("LIST_POP on non-ref");
        HeapObject* obj = heap.Get(static_cast<uint32_t>(v.i64));
        if (!obj || obj->header.kind != ObjectKind::List) return Trap("LIST_POP on non-list");
        uint32_t length = ReadU32Payload(obj->payload, 0);
        if (length == 0) return Trap("LIST_POP empty");
        uint32_t index = length - 1;
        size_t offset = 8 + static_cast<size_t>(index) * 4;
        int32_t value = static_cast<int32_t>(ReadU32Payload(obj->payload, offset));
        WriteU32Payload(obj->payload, 0, length - 1);
        Push(stack, Value{ValueKind::I32, value});
        break;
      }
      case OpCode::StringLen: {
        Value v = Pop(stack);
        if (v.kind != ValueKind::Ref || v.i64 < 0) return Trap("STRING_LEN on non-ref");
        HeapObject* obj = heap.Get(static_cast<uint32_t>(v.i64));
        if (!obj || obj->header.kind != ObjectKind::String) return Trap("STRING_LEN on non-string");
        uint32_t length = ReadU32Payload(obj->payload, 0);
        Push(stack, Value{ValueKind::I32, static_cast<int32_t>(length)});
        break;
      }
      case OpCode::StringConcat: {
        Value b = Pop(stack);
        Value a = Pop(stack);
        if (a.kind != ValueKind::Ref || b.kind != ValueKind::Ref) return Trap("STRING_CONCAT on non-ref");
        HeapObject* obj_a = heap.Get(static_cast<uint32_t>(a.i64));
        HeapObject* obj_b = heap.Get(static_cast<uint32_t>(b.i64));
        if (!obj_a || !obj_b || obj_a->header.kind != ObjectKind::String || obj_b->header.kind != ObjectKind::String) {
          return Trap("STRING_CONCAT on non-string");
        }
        std::u16string sa = ReadString(obj_a);
        std::u16string sb = ReadString(obj_b);
        std::u16string combined = sa + sb;
        uint32_t handle = CreateString(heap, combined);
        if (handle == 0xFFFFFFFFu) return Trap("STRING_CONCAT allocation failed");
        Push(stack, Value{ValueKind::Ref, static_cast<int64_t>(handle)});
        break;
      }
      case OpCode::CallCheck: {
        if (!call_stack.empty()) return Trap("CALLCHECK not in root");
        break;
      }
      case OpCode::Line: {
        ReadU32(module.code, pc);
        ReadU32(module.code, pc);
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
        ReadU32(module.code, pc);
        return Trap("INTRINSIC not supported");
      }
      case OpCode::SysCall: {
        ReadU32(module.code, pc);
        return Trap("SYS_CALL not supported");
      }
      case OpCode::AddI32:
      case OpCode::SubI32:
      case OpCode::MulI32:
      case OpCode::DivI32:
      case OpCode::ModI32: {
        Value b = Pop(stack);
        Value a = Pop(stack);
        if (a.kind != ValueKind::I32 || b.kind != ValueKind::I32) return Trap("I32 arithmetic on non-i32");
        int32_t lhs = static_cast<int32_t>(a.i64);
        int32_t rhs = static_cast<int32_t>(b.i64);
        int32_t out = 0;
        if (opcode == static_cast<uint8_t>(OpCode::AddI32)) out = lhs + rhs;
        if (opcode == static_cast<uint8_t>(OpCode::SubI32)) out = lhs - rhs;
        if (opcode == static_cast<uint8_t>(OpCode::MulI32)) out = lhs * rhs;
        if (opcode == static_cast<uint8_t>(OpCode::DivI32)) out = rhs == 0 ? 0 : (lhs / rhs);
        if (opcode == static_cast<uint8_t>(OpCode::ModI32)) out = rhs == 0 ? 0 : (lhs % rhs);
        Push(stack, Value{ValueKind::I32, out});
        break;
      }
      case OpCode::CmpEqI32:
      case OpCode::CmpLtI32:
      case OpCode::CmpNeI32:
      case OpCode::CmpLeI32:
      case OpCode::CmpGtI32:
      case OpCode::CmpGeI32: {
        Value b = Pop(stack);
        Value a = Pop(stack);
        if (a.kind != ValueKind::I32 || b.kind != ValueKind::I32) return Trap("I32 compare on non-i32");
        int32_t lhs = static_cast<int32_t>(a.i64);
        int32_t rhs = static_cast<int32_t>(b.i64);
        bool out = false;
        if (opcode == static_cast<uint8_t>(OpCode::CmpEqI32)) out = (lhs == rhs);
        if (opcode == static_cast<uint8_t>(OpCode::CmpNeI32)) out = (lhs != rhs);
        if (opcode == static_cast<uint8_t>(OpCode::CmpLtI32)) out = (lhs < rhs);
        if (opcode == static_cast<uint8_t>(OpCode::CmpLeI32)) out = (lhs <= rhs);
        if (opcode == static_cast<uint8_t>(OpCode::CmpGtI32)) out = (lhs > rhs);
        if (opcode == static_cast<uint8_t>(OpCode::CmpGeI32)) out = (lhs >= rhs);
        Push(stack, Value{ValueKind::Bool, out ? 1 : 0});
        break;
      }
      case OpCode::BoolNot: {
        Value v = Pop(stack);
        if (v.kind != ValueKind::Bool) return Trap("BOOL_NOT on non-bool");
        Push(stack, Value{ValueKind::Bool, v.i64 ? 0 : 1});
        break;
      }
      case OpCode::BoolAnd:
      case OpCode::BoolOr: {
        Value b = Pop(stack);
        Value a = Pop(stack);
        if (a.kind != ValueKind::Bool || b.kind != ValueKind::Bool) return Trap("BOOL op on non-bool");
        bool out = (opcode == static_cast<uint8_t>(OpCode::BoolAnd)) ? (a.i64 && b.i64) : (a.i64 || b.i64);
        Push(stack, Value{ValueKind::Bool, out ? 1 : 0});
        break;
      }
      case OpCode::Jmp: {
        int32_t rel = ReadI32(module.code, pc);
        pc = static_cast<size_t>(static_cast<int64_t>(pc) + rel);
        if (pc < func_start || pc > end) return Trap("JMP out of bounds");
        break;
      }
      case OpCode::JmpTrue:
      case OpCode::JmpFalse: {
        int32_t rel = ReadI32(module.code, pc);
        Value cond = Pop(stack);
        if (cond.kind != ValueKind::Bool) return Trap("JMP on non-bool");
        bool take = cond.i64 != 0;
        if (opcode == static_cast<uint8_t>(OpCode::JmpFalse)) take = !take;
        if (take) {
          pc = static_cast<size_t>(static_cast<int64_t>(pc) + rel);
          if (pc < func_start || pc > end) return Trap("JMP out of bounds");
        }
        break;
      }
      case OpCode::Enter: {
        uint16_t locals = ReadU16(module.code, pc);
        if (locals != current.locals.size()) return Trap("ENTER local count mismatch");
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

        std::vector<Value> args(arg_count);
        for (int i = static_cast<int>(arg_count) - 1; i >= 0; --i) {
          args[static_cast<size_t>(i)] = Pop(stack);
        }

        current.return_pc = pc;
        call_stack.push_back(current);
        current = setup_frame(func_id, pc, stack.size());
        for (size_t i = 0; i < args.size() && i < current.locals.size(); ++i) {
          current.locals[i] = args[i];
        }
        func_start = func.code_offset;
        pc = func_start;
        end = func_start + func.code_size;
        break;
      }
      case OpCode::Ret: {
        Value ret = {ValueKind::None, 0};
        if (!stack.empty()) ret = Pop(stack);
        if (call_stack.empty()) {
          ExecResult result;
          result.status = ExecStatus::Halted;
          if (ret.kind == ValueKind::I32) result.exit_code = static_cast<int32_t>(ret.i64);
          return result;
        }
        Frame caller = call_stack.back();
        call_stack.pop_back();
        stack.resize(caller.stack_base);
        if (ret.kind != ValueKind::None) Push(stack, ret);
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
  return result;
}

} // namespace simplevm
