#include "sbc_verifier.h"

#include <cstdint>
#include <unordered_map>
#include <unordered_set>

#include "opcode.h"
#include "intrinsic_ids.h"

namespace Simple::Byte {
namespace {
using namespace Simple::VM;

bool IsKnownIntrinsic(uint32_t id) {
  switch (id) {
    case kIntrinsicTrap:
    case kIntrinsicBreakpoint:
    case kIntrinsicLogI32:
    case kIntrinsicLogI64:
    case kIntrinsicLogF32:
    case kIntrinsicLogF64:
    case kIntrinsicLogRef:
    case kIntrinsicAbsI32:
    case kIntrinsicAbsI64:
    case kIntrinsicMinI32:
    case kIntrinsicMaxI32:
    case kIntrinsicMinI64:
    case kIntrinsicMaxI64:
    case kIntrinsicMinF32:
    case kIntrinsicMaxF32:
    case kIntrinsicMinF64:
    case kIntrinsicMaxF64:
    case kIntrinsicSqrtF32:
    case kIntrinsicSqrtF64:
    case kIntrinsicMonoNs:
    case kIntrinsicWallNs:
    case kIntrinsicRandU32:
    case kIntrinsicRandU64:
    case kIntrinsicWriteStdout:
    case kIntrinsicWriteStderr:
    case kIntrinsicPrintAny:
    case kIntrinsicDlCallI8:
    case kIntrinsicDlCallI16:
    case kIntrinsicDlCallI32:
    case kIntrinsicDlCallI64:
    case kIntrinsicDlCallU8:
    case kIntrinsicDlCallU16:
    case kIntrinsicDlCallU32:
    case kIntrinsicDlCallU64:
    case kIntrinsicDlCallF32:
    case kIntrinsicDlCallF64:
    case kIntrinsicDlCallBool:
    case kIntrinsicDlCallChar:
    case kIntrinsicDlCallStr0:
      return true;
    default:
      return false;
  }
}

struct IntrinsicSig {
  uint8_t ret = 0;
  uint8_t param_count = 0;
  uint8_t params[3]{0, 0, 0};
};

bool GetIntrinsicSig(uint32_t id, IntrinsicSig* out) {
  switch (id) {
    case kIntrinsicTrap: *out = {0, 1, {1, 0}}; return true; // trap(i32)
    case kIntrinsicBreakpoint: *out = {0, 0, {0, 0}}; return true; // breakpoint()
    case kIntrinsicLogI32: *out = {0, 1, {1, 0}}; return true; // log_i32(i32)
    case kIntrinsicLogI64: *out = {0, 1, {2, 0}}; return true; // log_i64(i64)
    case kIntrinsicLogF32: *out = {0, 1, {3, 0}}; return true; // log_f32(f32)
    case kIntrinsicLogF64: *out = {0, 1, {4, 0}}; return true; // log_f64(f64)
    case kIntrinsicLogRef: *out = {0, 1, {5, 0}}; return true; // log_ref(ref)
    case kIntrinsicAbsI32: *out = {1, 1, {1, 0}}; return true; // abs_i32(i32)->i32
    case kIntrinsicAbsI64: *out = {2, 1, {2, 0}}; return true; // abs_i64(i64)->i64
    case kIntrinsicMinI32: *out = {1, 2, {1, 1}}; return true; // min_i32(i32,i32)->i32
    case kIntrinsicMaxI32: *out = {1, 2, {1, 1}}; return true; // max_i32(i32,i32)->i32
    case kIntrinsicMinI64: *out = {2, 2, {2, 2}}; return true; // min_i64(i64,i64)->i64
    case kIntrinsicMaxI64: *out = {2, 2, {2, 2}}; return true; // max_i64(i64,i64)->i64
    case kIntrinsicMinF32: *out = {3, 2, {3, 3}}; return true; // min_f32(f32,f32)->f32
    case kIntrinsicMaxF32: *out = {3, 2, {3, 3}}; return true; // max_f32(f32,f32)->f32
    case kIntrinsicMinF64: *out = {4, 2, {4, 4}}; return true; // min_f64(f64,f64)->f64
    case kIntrinsicMaxF64: *out = {4, 2, {4, 4}}; return true; // max_f64(f64,f64)->f64
    case kIntrinsicSqrtF32: *out = {3, 1, {3, 0}}; return true; // sqrt_f32(f32)->f32
    case kIntrinsicSqrtF64: *out = {4, 1, {4, 0}}; return true; // sqrt_f64(f64)->f64
    case kIntrinsicMonoNs: *out = {2, 0, {0, 0}}; return true; // mono_ns()->i64
    case kIntrinsicWallNs: *out = {2, 0, {0, 0}}; return true; // wall_ns()->i64
    case kIntrinsicRandU32: *out = {1, 0, {0, 0}}; return true; // rand_u32()->i32
    case kIntrinsicRandU64: *out = {2, 0, {0, 0}}; return true; // rand_u64()->i64
    case kIntrinsicWriteStdout: *out = {0, 2, {5, 1}}; return true; // write_stdout(ref,i32)
    case kIntrinsicWriteStderr: *out = {0, 2, {5, 1}}; return true; // write_stderr(ref,i32)
    case kIntrinsicPrintAny: *out = {0, 2, {0, 1}}; return true; // print_any(any,i32_tag)
    case kIntrinsicDlCallI8: *out = {7, 3, {2, 7, 7}}; return true; // dl_call_i8(i64,i8,i8)->i8
    case kIntrinsicDlCallI16: *out = {8, 3, {2, 8, 8}}; return true; // dl_call_i16(i64,i16,i16)->i16
    case kIntrinsicDlCallI32: *out = {1, 3, {2, 1, 1}}; return true; // dl_call_i32(i64,i32,i32)->i32
    case kIntrinsicDlCallI64: *out = {2, 3, {2, 2, 2}}; return true; // dl_call_i64(i64,i64,i64)->i64
    case kIntrinsicDlCallU8: *out = {9, 3, {2, 9, 9}}; return true; // dl_call_u8(i64,u8,u8)->u8
    case kIntrinsicDlCallU16: *out = {10, 3, {2, 10, 10}}; return true; // dl_call_u16(i64,u16,u16)->u16
    case kIntrinsicDlCallU32: *out = {11, 3, {2, 11, 11}}; return true; // dl_call_u32(i64,u32,u32)->u32
    case kIntrinsicDlCallU64: *out = {12, 3, {2, 12, 12}}; return true; // dl_call_u64(i64,u64,u64)->u64
    case kIntrinsicDlCallF32: *out = {3, 3, {2, 3, 3}}; return true; // dl_call_f32(i64,f32,f32)->f32
    case kIntrinsicDlCallF64: *out = {4, 3, {2, 4, 4}}; return true; // dl_call_f64(i64,f64,f64)->f64
    case kIntrinsicDlCallBool: *out = {6, 3, {2, 6, 6}}; return true; // dl_call_bool(i64,bool,bool)->bool
    case kIntrinsicDlCallChar: *out = {13, 3, {2, 13, 13}}; return true; // dl_call_char(i64,char,char)->char
    case kIntrinsicDlCallStr0: *out = {5, 1, {2, 0, 0}}; return true; // dl_call_str0(i64)->ref
    default: return false;
  }
}

bool ReadI32(const std::vector<uint8_t>& code, size_t offset, int32_t* out) {
  if (offset + 4 > code.size()) return false;
  uint32_t v = static_cast<uint32_t>(code[offset]) |
               (static_cast<uint32_t>(code[offset + 1]) << 8) |
               (static_cast<uint32_t>(code[offset + 2]) << 16) |
               (static_cast<uint32_t>(code[offset + 3]) << 24);
  *out = static_cast<int32_t>(v);
  return true;
}

bool ReadU16(const std::vector<uint8_t>& code, size_t offset, uint16_t* out) {
  if (offset + 2 > code.size()) return false;
  *out = static_cast<uint16_t>(code[offset]) |
         (static_cast<uint16_t>(code[offset + 1]) << 8);
  return true;
}

bool ReadU32(const std::vector<uint8_t>& code, size_t offset, uint32_t* out) {
  if (offset + 4 > code.size()) return false;
  *out = static_cast<uint32_t>(code[offset]) |
         (static_cast<uint32_t>(code[offset + 1]) << 8) |
         (static_cast<uint32_t>(code[offset + 2]) << 16) |
         (static_cast<uint32_t>(code[offset + 3]) << 24);
  return true;
}

VerifyResult Fail(const std::string& message) {
  VerifyResult result;
  result.ok = false;
  result.error = message;
  return result;
}

} // namespace

VerifyResult VerifyModule(const SbcModule& module) {
  enum class ValType {
    Unknown,
    I8,
    I16,
    I32,
    I64,
    U8,
    U16,
    U32,
    U64,
    F32,
    F64,
    Bool,
    Char,
    Ref,
  };
  auto read_name = [&](uint32_t offset) -> std::string {
    if (offset == 0xFFFFFFFFu) return {};
    if (module.const_pool.empty()) return {};
    if (offset >= module.const_pool.size()) return {};
    size_t pos = offset;
    while (pos < module.const_pool.size() && module.const_pool[pos] != 0) {
      ++pos;
    }
    if (pos >= module.const_pool.size()) return {};
    return std::string(reinterpret_cast<const char*>(&module.const_pool[offset]), pos - offset);
  };
  auto resolve_type = [&](uint32_t type_id) -> ValType {
    if (type_id >= module.types.size()) return ValType::Unknown;
    const auto& row = module.types[type_id];
    switch (static_cast<TypeKind>(row.kind)) {
      case TypeKind::I8:
        return ValType::I8;
      case TypeKind::I16:
        return ValType::I16;
      case TypeKind::I32:
        return ValType::I32;
      case TypeKind::I64:
        return ValType::I64;
      case TypeKind::U8:
        return ValType::U8;
      case TypeKind::U16:
        return ValType::U16;
      case TypeKind::U32:
        return ValType::U32;
      case TypeKind::U64:
        return ValType::U64;
      case TypeKind::Bool:
        return ValType::Bool;
      case TypeKind::Char:
        return ValType::Char;
      case TypeKind::I128:
      case TypeKind::U128:
        return ValType::Ref;
      case TypeKind::F32:
        return ValType::F32;
      case TypeKind::F64:
        return ValType::F64;
      case TypeKind::Ref:
      case TypeKind::String:
        return ValType::Ref;
      case TypeKind::Unspecified:
        if ((row.flags & 0x1u) != 0u) return ValType::Ref;
        return ValType::Unknown;
      default:
        return ValType::Unknown;
    }
  };
  auto to_vm_type = [&](ValType t) -> VmType {
    switch (t) {
      case ValType::I8:
      case ValType::I16:
      case ValType::I32:
      case ValType::U8:
      case ValType::U16:
      case ValType::U32:
      case ValType::Bool:
      case ValType::Char:
        return VmType::I32;
      case ValType::U64:
      case ValType::I64:
        return VmType::I64;
      case ValType::F32:
        return VmType::F32;
      case ValType::F64:
        return VmType::F64;
      case ValType::Ref:
        return VmType::Ref;
      case ValType::Unknown:
      default:
        return VmType::Unknown;
    }
  };
  auto from_intrinsic_type = [&](uint8_t code) -> ValType {
    switch (code) {
      case 0: return ValType::Unknown;
      case 1: return ValType::I32;
      case 2: return ValType::I64;
      case 3: return ValType::F32;
      case 4: return ValType::F64;
      case 5: return ValType::Ref;
      case 6: return ValType::Bool;
      case 7: return ValType::I8;
      case 8: return ValType::I16;
      case 9: return ValType::U8;
      case 10: return ValType::U16;
      case 11: return ValType::U32;
      case 12: return ValType::U64;
      case 13: return ValType::Char;
      default: return ValType::Unknown;
    }
  };
  auto make_ref_bits = [&](const std::vector<ValType>& types) -> std::vector<uint8_t> {
    size_t count = types.size();
    std::vector<uint8_t> bits((count + 7) / 8, 0);
    for (size_t i = 0; i < count; ++i) {
      if (types[i] == ValType::Ref) {
        bits[i / 8] |= static_cast<uint8_t>(1u << (i % 8));
      }
    }
    return bits;
  };
  auto make_ref_bits_vm = [&](const std::vector<VmType>& types) -> std::vector<uint8_t> {
    size_t count = types.size();
    std::vector<uint8_t> bits((count + 7) / 8, 0);
    for (size_t i = 0; i < count; ++i) {
      if (types[i] == VmType::Ref) {
        bits[i / 8] |= static_cast<uint8_t>(1u << (i % 8));
      }
    }
    return bits;
  };

  std::vector<ValType> global_types(module.globals.size(), ValType::Unknown);
  std::vector<bool> globals_init_base(module.globals.size(), false);
  for (size_t i = 0; i < module.globals.size(); ++i) {
    ValType global_type = resolve_type(module.globals[i].type_id);
    if (global_type != ValType::Ref) {
      global_type = ValType::Unknown;
    }
    global_types[i] = global_type;
    if (module.globals[i].init_const_id != 0xFFFFFFFFu) globals_init_base[i] = true;
  }

  VerifyResult result;
  result.methods.resize(module.functions.size());
  result.globals_ref_bits = make_ref_bits(global_types);

  const auto& code = module.code;
  for (size_t func_index = 0; func_index < module.functions.size(); ++func_index) {
    const auto& func = module.functions[func_index];
    if (func.code_offset + func.code_size > code.size()) {
      return Fail("function code out of bounds");
    }

    size_t pc = func.code_offset;
    size_t end = func.code_offset + func.code_size;
    std::unordered_set<size_t> boundaries;

    uint32_t method_id = func.method_id;
    if (method_id >= module.methods.size()) return Fail("function method id out of range");
    uint16_t local_count = module.methods[method_id].local_count;
    uint32_t method_sig_id = module.methods[method_id].sig_id;
    if (method_sig_id >= module.sigs.size()) return Fail("function signature out of range");
    const auto& sig = module.sigs[method_sig_id];
    uint32_t ret_type_id = sig.ret_type_id;

    bool expect_void = (ret_type_id == 0xFFFFFFFFu);
    ValType expected_ret = expect_void ? ValType::Unknown : resolve_type(ret_type_id);
    if (!expect_void && expected_ret == ValType::Unknown) return Fail("unsupported return type");

    auto scan_fail = [&](const std::string& msg, size_t at_pc, uint8_t opcode) -> VerifyResult {
      std::string out = "verify failed: func " + std::to_string(func_index);
      std::string name = read_name(module.methods[method_id].name_str);
      if (!name.empty()) {
        out += " name " + name;
      }
      out += " pc " + std::to_string(at_pc);
      out += " op 0x";
      static const char kHex[] = "0123456789ABCDEF";
      out.push_back(kHex[(opcode >> 4) & 0xF]);
      out.push_back(kHex[opcode & 0xF]);
      const char* op_name = OpCodeName(opcode);
      if (op_name && op_name[0] != '\0') {
        out += " ";
        out += op_name;
      }
      out += ": ";
      out += msg;
      return Fail(out);
    };
    while (pc < end) {
      boundaries.insert(pc);
      uint8_t opcode = code[pc];
      OpInfo info{};
      if (!GetOpInfo(opcode, &info)) {
        return scan_fail("unknown opcode in verifier", pc - func.code_offset, opcode);
      }
      size_t next = pc + 1 + static_cast<size_t>(info.operand_bytes);
      if (next > end) {
        return scan_fail("opcode operands out of bounds", pc - func.code_offset, opcode);
      }
      pc = next;
    }

    boundaries.insert(end);
    if (pc != end) return Fail("function code does not align to instruction boundary");

    pc = func.code_offset;
    int stack_height = 0;
    std::unordered_map<size_t, std::vector<ValType>> merge_types;
    std::vector<ValType> stack_types;
    std::vector<ValType> locals(local_count, ValType::Unknown);
    std::vector<bool> locals_init(local_count, false);
    if (sig.param_count > local_count) return Fail("param count exceeds locals");
    if (sig.param_count > 0 &&
        sig.param_type_start + sig.param_count > module.param_types.size()) {
      return Fail("signature param types out of range");
    }
    for (uint16_t i = 0; i < sig.param_count && i < locals_init.size(); ++i) {
      uint32_t type_id = module.param_types[sig.param_type_start + i];
      ValType param_type = resolve_type(type_id);
      if (param_type == ValType::Unknown) return Fail("unsupported param type");
      locals[i] = param_type;
      locals_init[i] = true;
    }
    std::vector<ValType> globals = global_types;
    std::vector<bool> globals_init = globals_init_base;
    int call_depth = 0;
    auto fail_at = [&](const std::string& msg, size_t at_pc, uint8_t opcode) -> VerifyResult {
      std::string out = "verify failed: func " + std::to_string(func_index);
      std::string name = read_name(module.methods[method_id].name_str);
      if (!name.empty()) {
        out += " name " + name;
      }
      out += " pc " + std::to_string(at_pc);
      out += " op 0x";
      static const char kHex[] = "0123456789ABCDEF";
      out.push_back(kHex[(opcode >> 4) & 0xF]);
      out.push_back(kHex[opcode & 0xF]);
      const char* op_name = OpCodeName(opcode);
      if (op_name && op_name[0] != '\0') {
        out += " ";
        out += op_name;
      }
      out += ": ";
      out += msg;
      return Fail(out);
    };
    size_t current_pc = 0;
    uint8_t current_opcode = 0;
    auto pop_type = [&]() -> ValType {
      if (stack_types.empty()) return ValType::Unknown;
      ValType t = stack_types.back();
      stack_types.pop_back();
      return t;
    };
    auto push_type = [&](ValType t) { stack_types.push_back(t); };
    auto check_type = [&](ValType got, ValType expected, const char* msg) -> VerifyResult {
      if (expected == ValType::Unknown) {
        VerifyResult ok;
        ok.ok = true;
        return ok;
      }
      if (got != ValType::Unknown && got != expected) {
        return fail_at(msg, current_pc, current_opcode);
      }
      VerifyResult ok;
      ok.ok = true;
      return ok;
    };
    auto is_i32_numeric_type = [&](ValType t) {
      return t == ValType::I8 || t == ValType::I16 || t == ValType::I32 ||
             t == ValType::U8 || t == ValType::U16 || t == ValType::U32 ||
             t == ValType::Char;
    };
    auto is_i32_arith_type = [&](ValType t) {
      return is_i32_numeric_type(t);
    };
    auto is_u32_arith_type = [&](ValType t) {
      return t == ValType::U8 || t == ValType::U16 || t == ValType::U32;
    };
    auto is_i32_bitwise_type = [&](ValType t) {
      return is_i32_arith_type(t) || is_u32_arith_type(t);
    };
    auto is_i64_bitwise_type = [&](ValType t) {
      return t == ValType::I64 || t == ValType::U64;
    };
    std::vector<StackMap> stack_maps;
    while (pc < end) {
      uint8_t opcode = code[pc];
      current_pc = pc;
      current_opcode = opcode;
      OpInfo info{};
      GetOpInfo(opcode, &info);
      size_t next = pc + 1 + static_cast<size_t>(info.operand_bytes);
      if (opcode == static_cast<uint8_t>(OpCode::Line) ||
          opcode == static_cast<uint8_t>(OpCode::ProfileStart) ||
          opcode == static_cast<uint8_t>(OpCode::ProfileEnd)) {
        StackMap map;
        map.pc = static_cast<uint32_t>(pc);
        map.stack_height = static_cast<uint32_t>(stack_types.size());
        map.ref_bits = make_ref_bits(stack_types);
        stack_maps.push_back(std::move(map));
      }

      std::vector<size_t> jump_targets;
      bool fall_through = true;
      int extra_pops = 0;
      int extra_pushes = 0;
      if (opcode == static_cast<uint8_t>(OpCode::Jmp) ||
          opcode == static_cast<uint8_t>(OpCode::JmpTrue) ||
          opcode == static_cast<uint8_t>(OpCode::JmpFalse)) {
        int32_t offset = 0;
        if (!ReadI32(code, pc + 1, &offset)) {
          return fail_at("jump operand out of bounds", pc, opcode);
        }
        size_t jump_target = static_cast<size_t>(static_cast<int64_t>(next) + offset);
        if (jump_target < func.code_offset || jump_target > end) {
          return fail_at("jump target out of bounds", pc, opcode);
        }
        if (boundaries.find(jump_target) == boundaries.end()) {
          return fail_at("jump target not on instruction boundary", pc, opcode);
        }
        jump_targets.push_back(jump_target);
      }
      if (opcode == static_cast<uint8_t>(OpCode::JmpTable)) {
        uint32_t const_id = 0;
        int32_t default_off = 0;
        if (!ReadU32(code, pc + 1, &const_id)) {
          return fail_at("JMP_TABLE const id out of bounds", pc, opcode);
        }
        if (!ReadI32(code, pc + 5, &default_off)) {
          return fail_at("JMP_TABLE default offset out of bounds", pc, opcode);
        }
        if (const_id + 8 > module.const_pool.size()) {
          return fail_at("JMP_TABLE const id bad", pc, opcode);
        }
        uint32_t kind = 0;
        ReadU32(module.const_pool, const_id, &kind);
        if (kind != 6) return fail_at("JMP_TABLE const kind mismatch", pc, opcode);
        uint32_t payload = 0;
        ReadU32(module.const_pool, const_id + 4, &payload);
        if (payload + 4 > module.const_pool.size()) {
          return fail_at("JMP_TABLE blob out of bounds", pc, opcode);
        }
        uint32_t blob_len = 0;
        ReadU32(module.const_pool, payload, &blob_len);
        if (payload + 4 + blob_len > module.const_pool.size()) {
          return fail_at("JMP_TABLE blob out of bounds", pc, opcode);
        }
        if (blob_len < 4 || (blob_len - 4) % 4 != 0) {
          return fail_at("JMP_TABLE blob size invalid", pc, opcode);
        }
        uint32_t count = 0;
        ReadU32(module.const_pool, payload + 4, &count);
        if (blob_len != 4 + count * 4) {
          return fail_at("JMP_TABLE blob size mismatch", pc, opcode);
        }
        for (uint32_t i = 0; i < count; ++i) {
          int32_t off = 0;
          ReadI32(module.const_pool, payload + 8 + i * 4, &off);
          size_t target = static_cast<size_t>(static_cast<int64_t>(next) + off);
          if (target < func.code_offset || target > end) {
            return fail_at("jump target out of bounds", pc, opcode);
          }
          if (boundaries.find(target) == boundaries.end()) {
            return fail_at("jump target not on instruction boundary", pc, opcode);
          }
          jump_targets.push_back(target);
        }
        size_t default_target = static_cast<size_t>(static_cast<int64_t>(next) + default_off);
        if (default_target < func.code_offset || default_target > end) {
          return fail_at("jump target out of bounds", pc, opcode);
        }
        if (boundaries.find(default_target) == boundaries.end()) {
          return fail_at("jump target not on instruction boundary", pc, opcode);
        }
        jump_targets.push_back(default_target);
      }

      if (opcode == static_cast<uint8_t>(OpCode::Enter)) {
        uint16_t enter_locals = 0;
        if (!ReadU16(code, pc + 1, &enter_locals)) return fail_at("ENTER operand out of bounds", pc, opcode);
        if (enter_locals != local_count) return fail_at("ENTER local count mismatch", pc, opcode);
      }
      if (opcode == static_cast<uint8_t>(OpCode::LoadLocal) ||
          opcode == static_cast<uint8_t>(OpCode::StoreLocal)) {
        uint32_t idx = 0;
        if (!ReadU32(code, pc + 1, &idx)) return fail_at("local index out of bounds", pc, opcode);
        if (idx >= local_count) return fail_at("local index out of range", pc, opcode);
      }
      if (opcode == static_cast<uint8_t>(OpCode::LoadGlobal) ||
          opcode == static_cast<uint8_t>(OpCode::StoreGlobal)) {
        uint32_t idx = 0;
        if (!ReadU32(code, pc + 1, &idx)) return fail_at("global index out of bounds", pc, opcode);
        if (idx >= module.globals.size()) return fail_at("global index out of range", pc, opcode);
      }
      if (opcode == static_cast<uint8_t>(OpCode::LoadUpvalue) ||
          opcode == static_cast<uint8_t>(OpCode::StoreUpvalue)) {
        uint32_t idx = 0;
        if (!ReadU32(code, pc + 1, &idx)) return fail_at("upvalue index out of bounds", pc, opcode);
      }
      if (opcode == static_cast<uint8_t>(OpCode::NewObject)) {
        uint32_t type_id = 0;
        if (!ReadU32(code, pc + 1, &type_id)) return fail_at("NEW_OBJECT type id out of bounds", pc, opcode);
        if (type_id >= module.types.size()) return fail_at("NEW_OBJECT bad type id", pc, opcode);
      }
      if (opcode == static_cast<uint8_t>(OpCode::NewClosure)) {
          uint32_t closure_method_id = 0;
          if (!ReadU32(code, pc + 1, &closure_method_id)) {
            return fail_at("NEW_CLOSURE method id out of bounds", pc, opcode);
          }
          if (pc + 5 >= code.size()) return fail_at("NEW_CLOSURE upvalue count out of bounds", pc, opcode);
          if (closure_method_id >= module.methods.size()) return fail_at("NEW_CLOSURE bad method id", pc, opcode);
      }
      if (opcode == static_cast<uint8_t>(OpCode::NewArray) ||
          opcode == static_cast<uint8_t>(OpCode::NewArrayI64) ||
          opcode == static_cast<uint8_t>(OpCode::NewArrayF32) ||
          opcode == static_cast<uint8_t>(OpCode::NewArrayF64) ||
          opcode == static_cast<uint8_t>(OpCode::NewArrayRef) ||
          opcode == static_cast<uint8_t>(OpCode::NewList) ||
          opcode == static_cast<uint8_t>(OpCode::NewListI64) ||
          opcode == static_cast<uint8_t>(OpCode::NewListF32) ||
          opcode == static_cast<uint8_t>(OpCode::NewListF64) ||
          opcode == static_cast<uint8_t>(OpCode::NewListRef)) {
        uint32_t type_id = 0;
        if (!ReadU32(code, pc + 1, &type_id)) {
          return fail_at("NEW_ARRAY/LIST type id out of bounds", pc, opcode);
        }
        if (type_id >= module.types.size()) return fail_at("NEW_ARRAY/LIST bad type id", pc, opcode);
      }
      if (opcode == static_cast<uint8_t>(OpCode::LoadField) ||
          opcode == static_cast<uint8_t>(OpCode::StoreField)) {
        uint32_t field_id = 0;
        if (!ReadU32(code, pc + 1, &field_id)) {
          return fail_at("LOAD/STORE_FIELD id out of bounds", pc, opcode);
        }
        if (field_id >= module.fields.size()) return fail_at("LOAD/STORE_FIELD bad field id", pc, opcode);
      }
      if (opcode == static_cast<uint8_t>(OpCode::ConstString)) {
        uint32_t const_id = 0;
        if (!ReadU32(code, pc + 1, &const_id)) return fail_at("CONST_STRING const id out of bounds", pc, opcode);
        if (const_id + 8 > module.const_pool.size()) return fail_at("CONST_STRING const id bad", pc, opcode);
      }
      if (opcode == static_cast<uint8_t>(OpCode::Call) ||
          opcode == static_cast<uint8_t>(OpCode::TailCall)) {
        uint32_t func_id = 0;
        uint8_t arg_count = 0;
        if (!ReadU32(code, pc + 1, &func_id)) return fail_at("CALL function id out of bounds", pc, opcode);
        if (pc + 5 >= code.size()) return fail_at("CALL arg count out of bounds", pc, opcode);
        arg_count = code[pc + 5];
        if (func_id >= module.functions.size()) return fail_at("CALL function id out of range", pc, opcode);
        uint32_t callee_method = module.functions[func_id].method_id;
        if (callee_method >= module.methods.size()) return fail_at("CALL method id out of range", pc, opcode);
        uint32_t sig_id = module.methods[callee_method].sig_id;
        if (sig_id >= module.sigs.size()) return fail_at("CALL signature id out of range", pc, opcode);
        if (arg_count != module.sigs[sig_id].param_count) {
          return fail_at("CALL arg count mismatch", pc, opcode);
        }
        if (opcode == static_cast<uint8_t>(OpCode::Call)) ++call_depth;
      }
      if (opcode == static_cast<uint8_t>(OpCode::CallIndirect)) {
        uint32_t sig_id = 0;
        if (!ReadU32(code, pc + 1, &sig_id)) return fail_at("CALL_INDIRECT sig id out of bounds", pc, opcode);
        if (pc + 5 >= code.size()) return fail_at("CALL_INDIRECT arg count out of bounds", pc, opcode);
        uint8_t arg_count = code[pc + 5];
        if (sig_id >= module.sigs.size()) return fail_at("CALL_INDIRECT signature id out of range", pc, opcode);
        if (arg_count != module.sigs[sig_id].param_count) {
          return fail_at("CALL_INDIRECT arg count mismatch", pc, opcode);
        }
      }

      switch (static_cast<OpCode>(opcode)) {
        case OpCode::Jmp:
          fall_through = false;
          break;
        case OpCode::JmpTable: {
          ValType idx = pop_type();
          VerifyResult r = check_type(idx, ValType::I32, "JMP_TABLE index type mismatch");
          if (!r.ok) return r;
          fall_through = false;
          break;
        }
        case OpCode::ConstI8:
          push_type(ValType::I8);
          break;
        case OpCode::ConstI16:
          push_type(ValType::I16);
          break;
        case OpCode::ConstI32:
          push_type(ValType::I32);
          break;
        case OpCode::ConstI64:
          push_type(ValType::I64);
          break;
        case OpCode::ConstU8:
          push_type(ValType::U8);
          break;
        case OpCode::ConstU16:
          push_type(ValType::U16);
          break;
        case OpCode::ConstU32:
          push_type(ValType::U32);
          break;
        case OpCode::ConstU64:
          push_type(ValType::U64);
          break;
        case OpCode::ConstChar:
          push_type(ValType::Char);
          break;
        case OpCode::ConstI128:
        case OpCode::ConstU128:
          push_type(ValType::Ref);
          break;
        case OpCode::ConstF32:
          push_type(ValType::F32);
          break;
        case OpCode::ConstF64:
          push_type(ValType::F64);
          break;
        case OpCode::ConstBool:
          push_type(ValType::Bool);
          break;
        case OpCode::ConstNull:
        case OpCode::ConstString:
        case OpCode::NewObject:
        case OpCode::NewArray:
        case OpCode::NewArrayI64:
        case OpCode::NewArrayF32:
        case OpCode::NewArrayF64:
        case OpCode::NewArrayRef:
        case OpCode::NewList:
        case OpCode::NewListI64:
        case OpCode::NewListF32:
        case OpCode::NewListF64:
        case OpCode::NewListRef:
          push_type(ValType::Ref);
          break;
        case OpCode::NewClosure: {
          if (pc + 5 >= code.size()) return Fail("NEW_CLOSURE upvalue count out of bounds");
          uint8_t upvalue_count = code[pc + 5];
          for (int i = 0; i < static_cast<int>(upvalue_count); ++i) {
            ValType t = pop_type();
            VerifyResult r = check_type(t, ValType::Ref, "NEW_CLOSURE upvalue type mismatch");
            if (!r.ok) return r;
          }
          push_type(ValType::Ref);
          break;
        }
        case OpCode::LoadLocal: {
          uint32_t idx = 0;
          ReadU32(code, pc + 1, &idx);
          if (idx < locals.size()) {
            if (!locals_init[idx]) return Fail("LOAD_LOCAL uninitialized");
            push_type(locals[idx]);
          } else {
            push_type(ValType::Unknown);
          }
          break;
        }
        case OpCode::StoreLocal: {
          uint32_t idx = 0;
          ReadU32(code, pc + 1, &idx);
          ValType t = pop_type();
          if (idx < locals.size()) {
            if (locals[idx] != ValType::Unknown && t != ValType::Unknown && locals[idx] != t) {
              return Fail("STORE_LOCAL type mismatch");
            }
            locals[idx] = t;
            locals_init[idx] = true;
          }
          break;
        }
        case OpCode::LoadGlobal: {
          uint32_t idx = 0;
          ReadU32(code, pc + 1, &idx);
          if (idx < globals.size()) {
            if (!globals_init[idx]) return Fail("LOAD_GLOBAL uninitialized");
            push_type(globals[idx]);
          }
          else push_type(ValType::Unknown);
          break;
        }
        case OpCode::StoreGlobal: {
          uint32_t idx = 0;
          ReadU32(code, pc + 1, &idx);
          ValType t = pop_type();
          if (idx < globals.size()) {
            if (globals[idx] != ValType::Unknown && t != ValType::Unknown && globals[idx] != t) {
              return Fail("STORE_GLOBAL type mismatch");
            }
            globals[idx] = t;
            globals_init[idx] = true;
          }
          break;
        }
        case OpCode::LoadUpvalue:
          push_type(ValType::Ref);
          break;
        case OpCode::StoreUpvalue: {
          ValType t = pop_type();
          VerifyResult r = check_type(t, ValType::Ref, "STORE_UPVALUE type mismatch");
          if (!r.ok) return r;
          break;
        }
        case OpCode::Pop:
          pop_type();
          break;
        case OpCode::Dup: {
          if (stack_types.empty()) return Fail("DUP underflow");
          push_type(stack_types.back());
          break;
        }
        case OpCode::Dup2: {
          if (stack_types.size() < 2) return Fail("DUP2 underflow");
          ValType a = stack_types[stack_types.size() - 2];
          ValType b = stack_types[stack_types.size() - 1];
          push_type(a);
          push_type(b);
          break;
        }
        case OpCode::Swap: {
          if (stack_types.size() < 2) return Fail("SWAP underflow");
          std::swap(stack_types[stack_types.size() - 1], stack_types[stack_types.size() - 2]);
          break;
        }
        case OpCode::Rot: {
          if (stack_types.size() < 3) return Fail("ROT underflow");
          ValType a = stack_types[stack_types.size() - 3];
          ValType b = stack_types[stack_types.size() - 2];
          ValType c = stack_types[stack_types.size() - 1];
          stack_types[stack_types.size() - 3] = b;
          stack_types[stack_types.size() - 2] = c;
          stack_types[stack_types.size() - 1] = a;
          break;
        }
        case OpCode::AddI32:
        case OpCode::SubI32:
        case OpCode::MulI32:
        case OpCode::DivI32:
        case OpCode::ModI32: {
          ValType b = pop_type();
          ValType a = pop_type();
          if (a != ValType::Unknown && b != ValType::Unknown) {
            if (!is_i32_arith_type(a) || !is_i32_arith_type(b)) {
              return fail_at("arith type mismatch", current_pc, current_opcode);
            }
          }
          if (a == ValType::Unknown || b == ValType::Unknown) {
            push_type(ValType::Unknown);
          } else {
            push_type((a == b) ? a : ValType::I32);
          }
          break;
        }
        case OpCode::NegI32: {
          ValType a = pop_type();
          if (a != ValType::Unknown && !is_i32_arith_type(a)) {
            return fail_at("arith type mismatch", current_pc, current_opcode);
          }
          push_type(a);
          break;
        }
        case OpCode::IncI32:
        case OpCode::DecI32: {
          ValType a = pop_type();
          if (a != ValType::Unknown && !is_i32_arith_type(a)) {
            return fail_at("arith type mismatch", current_pc, current_opcode);
          }
          push_type(a);
          break;
        }
        case OpCode::AddU32:
        case OpCode::SubU32:
        case OpCode::MulU32:
        case OpCode::DivU32:
        case OpCode::ModU32: {
          ValType b = pop_type();
          ValType a = pop_type();
          if (a != ValType::Unknown && b != ValType::Unknown) {
            if (!is_u32_arith_type(a) || !is_u32_arith_type(b)) {
              return fail_at("arith type mismatch", current_pc, current_opcode);
            }
          }
          if (a == ValType::Unknown || b == ValType::Unknown) {
            push_type(ValType::Unknown);
          } else {
            push_type((a == b) ? a : ValType::U32);
          }
          break;
        }
        case OpCode::IncU32:
        case OpCode::DecU32: {
          ValType a = pop_type();
          if (a != ValType::Unknown && !is_u32_arith_type(a)) {
            return fail_at("arith type mismatch", current_pc, current_opcode);
          }
          push_type(a);
          break;
        }
        case OpCode::NegU32: {
          ValType a = pop_type();
          if (a != ValType::Unknown && !is_u32_arith_type(a)) {
            return fail_at("arith type mismatch", current_pc, current_opcode);
          }
          push_type(a);
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
          ValType a = pop_type();
          ValType expected = ValType::Unknown;
          ValType pushed = ValType::Unknown;
          switch (static_cast<OpCode>(opcode)) {
            case OpCode::IncI8:
            case OpCode::DecI8:
            case OpCode::NegI8:
              expected = ValType::I8;
              pushed = ValType::I8;
              break;
            case OpCode::IncI16:
            case OpCode::DecI16:
            case OpCode::NegI16:
              expected = ValType::I16;
              pushed = ValType::I16;
              break;
            case OpCode::IncU8:
            case OpCode::DecU8:
            case OpCode::NegU8:
              expected = ValType::U8;
              pushed = ValType::U8;
              break;
            case OpCode::IncU16:
            case OpCode::DecU16:
            case OpCode::NegU16:
              expected = ValType::U16;
              pushed = ValType::U16;
              break;
            default:
              break;
          }
          VerifyResult r = check_type(a, expected, "arith type mismatch");
          if (!r.ok) return r;
          push_type(pushed);
          break;
        }
        case OpCode::AndI32:
        case OpCode::OrI32:
        case OpCode::XorI32:
        case OpCode::ShlI32:
        case OpCode::ShrI32: {
          ValType b = pop_type();
          ValType a = pop_type();
          if (a != ValType::Unknown && b != ValType::Unknown) {
            if (!is_i32_bitwise_type(a) || !is_i32_bitwise_type(b)) {
              return fail_at("bitwise type mismatch", current_pc, current_opcode);
            }
          }
          if (a == ValType::Unknown || b == ValType::Unknown) {
            push_type(ValType::Unknown);
          } else {
            push_type((a == b) ? a : ValType::I32);
          }
          break;
        }
        case OpCode::AddI64:
        case OpCode::SubI64:
        case OpCode::MulI64:
        case OpCode::DivI64:
        case OpCode::ModI64: {
          ValType b = pop_type();
          ValType a = pop_type();
          VerifyResult r1 = check_type(a, ValType::I64, "arith type mismatch");
          if (!r1.ok) return r1;
          VerifyResult r2 = check_type(b, ValType::I64, "arith type mismatch");
          if (!r2.ok) return r2;
          push_type(ValType::I64);
          break;
        }
        case OpCode::NegI64: {
          ValType a = pop_type();
          VerifyResult r = check_type(a, ValType::I64, "arith type mismatch");
          if (!r.ok) return r;
          push_type(ValType::I64);
          break;
        }
        case OpCode::IncI64:
        case OpCode::DecI64: {
          ValType a = pop_type();
          VerifyResult r = check_type(a, ValType::I64, "arith type mismatch");
          if (!r.ok) return r;
          push_type(ValType::I64);
          break;
        }
        case OpCode::AddU64:
        case OpCode::SubU64:
        case OpCode::MulU64:
        case OpCode::DivU64:
        case OpCode::ModU64: {
          ValType b = pop_type();
          ValType a = pop_type();
          VerifyResult r1 = check_type(a, ValType::U64, "arith type mismatch");
          if (!r1.ok) return r1;
          VerifyResult r2 = check_type(b, ValType::U64, "arith type mismatch");
          if (!r2.ok) return r2;
          push_type(ValType::U64);
          break;
        }
        case OpCode::IncU64:
        case OpCode::DecU64: {
          ValType a = pop_type();
          VerifyResult r = check_type(a, ValType::U64, "arith type mismatch");
          if (!r.ok) return r;
          push_type(ValType::U64);
          break;
        }
        case OpCode::NegU64: {
          ValType a = pop_type();
          VerifyResult r = check_type(a, ValType::U64, "arith type mismatch");
          if (!r.ok) return r;
          push_type(ValType::U64);
          break;
        }
        case OpCode::AndI64:
        case OpCode::OrI64:
        case OpCode::XorI64:
        case OpCode::ShlI64:
        case OpCode::ShrI64: {
          ValType b = pop_type();
          ValType a = pop_type();
          if (a != ValType::Unknown && b != ValType::Unknown) {
            if (!is_i64_bitwise_type(a) || !is_i64_bitwise_type(b)) {
              return fail_at("bitwise type mismatch", current_pc, current_opcode);
            }
          }
          if (a == ValType::Unknown || b == ValType::Unknown) {
            push_type(ValType::Unknown);
          } else {
            push_type((a == b) ? a : ValType::I64);
          }
          break;
        }
        case OpCode::AddF32:
        case OpCode::SubF32:
        case OpCode::MulF32:
        case OpCode::DivF32: {
          ValType b = pop_type();
          ValType a = pop_type();
          VerifyResult r1 = check_type(a, ValType::F32, "arith type mismatch");
          if (!r1.ok) return r1;
          VerifyResult r2 = check_type(b, ValType::F32, "arith type mismatch");
          if (!r2.ok) return r2;
          push_type(ValType::F32);
          break;
        }
        case OpCode::NegF32: {
          ValType a = pop_type();
          VerifyResult r = check_type(a, ValType::F32, "arith type mismatch");
          if (!r.ok) return r;
          push_type(ValType::F32);
          break;
        }
        case OpCode::IncF32:
        case OpCode::DecF32: {
          ValType a = pop_type();
          VerifyResult r = check_type(a, ValType::F32, "arith type mismatch");
          if (!r.ok) return r;
          push_type(ValType::F32);
          break;
        }
        case OpCode::AddF64:
        case OpCode::SubF64:
        case OpCode::MulF64:
        case OpCode::DivF64: {
          ValType b = pop_type();
          ValType a = pop_type();
          VerifyResult r1 = check_type(a, ValType::F64, "arith type mismatch");
          if (!r1.ok) return r1;
          VerifyResult r2 = check_type(b, ValType::F64, "arith type mismatch");
          if (!r2.ok) return r2;
          push_type(ValType::F64);
          break;
        }
        case OpCode::NegF64: {
          ValType a = pop_type();
          VerifyResult r = check_type(a, ValType::F64, "arith type mismatch");
          if (!r.ok) return r;
          push_type(ValType::F64);
          break;
        }
        case OpCode::IncF64:
        case OpCode::DecF64: {
          ValType a = pop_type();
          VerifyResult r = check_type(a, ValType::F64, "arith type mismatch");
          if (!r.ok) return r;
          push_type(ValType::F64);
          break;
        }
        case OpCode::CmpEqI32:
        case OpCode::CmpNeI32:
        case OpCode::CmpLtI32:
        case OpCode::CmpLeI32:
        case OpCode::CmpGtI32:
        case OpCode::CmpGeI32: {
          ValType b = pop_type();
          ValType a = pop_type();
          if (a != ValType::Unknown && b != ValType::Unknown) {
            if (!is_i32_numeric_type(a) || !is_i32_numeric_type(b)) {
              return fail_at("compare type mismatch", current_pc, current_opcode);
            }
          }
          push_type(ValType::Bool);
          break;
        }
        case OpCode::CmpEqU32:
        case OpCode::CmpNeU32:
        case OpCode::CmpLtU32:
        case OpCode::CmpLeU32:
        case OpCode::CmpGtU32:
        case OpCode::CmpGeU32: {
          ValType b = pop_type();
          ValType a = pop_type();
          if (a != ValType::Unknown && b != ValType::Unknown) {
            if (!is_u32_arith_type(a) || !is_u32_arith_type(b)) {
              return fail_at("compare type mismatch", current_pc, current_opcode);
            }
          }
          push_type(ValType::Bool);
          break;
        }
        case OpCode::CmpEqI64:
        case OpCode::CmpNeI64:
        case OpCode::CmpLtI64:
        case OpCode::CmpLeI64:
        case OpCode::CmpGtI64:
        case OpCode::CmpGeI64: {
          ValType b = pop_type();
          ValType a = pop_type();
          VerifyResult r1 = check_type(a, ValType::I64, "compare type mismatch");
          if (!r1.ok) return r1;
          VerifyResult r2 = check_type(b, ValType::I64, "compare type mismatch");
          if (!r2.ok) return r2;
          push_type(ValType::Bool);
          break;
        }
        case OpCode::CmpEqU64:
        case OpCode::CmpNeU64:
        case OpCode::CmpLtU64:
        case OpCode::CmpLeU64:
        case OpCode::CmpGtU64:
        case OpCode::CmpGeU64: {
          ValType b = pop_type();
          ValType a = pop_type();
          VerifyResult r1 = check_type(a, ValType::U64, "compare type mismatch");
          if (!r1.ok) return r1;
          VerifyResult r2 = check_type(b, ValType::U64, "compare type mismatch");
          if (!r2.ok) return r2;
          push_type(ValType::Bool);
          break;
        }
        case OpCode::CmpEqF32:
        case OpCode::CmpNeF32:
        case OpCode::CmpLtF32:
        case OpCode::CmpLeF32:
        case OpCode::CmpGtF32:
        case OpCode::CmpGeF32: {
          ValType b = pop_type();
          ValType a = pop_type();
          VerifyResult r1 = check_type(a, ValType::F32, "compare type mismatch");
          if (!r1.ok) return r1;
          VerifyResult r2 = check_type(b, ValType::F32, "compare type mismatch");
          if (!r2.ok) return r2;
          push_type(ValType::Bool);
          break;
        }
        case OpCode::CmpEqF64:
        case OpCode::CmpNeF64:
        case OpCode::CmpLtF64:
        case OpCode::CmpLeF64:
        case OpCode::CmpGtF64:
        case OpCode::CmpGeF64: {
          ValType b = pop_type();
          ValType a = pop_type();
          VerifyResult r1 = check_type(a, ValType::F64, "compare type mismatch");
          if (!r1.ok) return r1;
          VerifyResult r2 = check_type(b, ValType::F64, "compare type mismatch");
          if (!r2.ok) return r2;
          push_type(ValType::Bool);
          break;
        }
        case OpCode::BoolNot: {
          ValType a = pop_type();
          VerifyResult r = check_type(a, ValType::Bool, "BOOL_NOT type mismatch");
          if (!r.ok) return r;
          push_type(ValType::Bool);
          break;
        }
        case OpCode::BoolAnd:
        case OpCode::BoolOr: {
          ValType b = pop_type();
          ValType a = pop_type();
          VerifyResult r1 = check_type(a, ValType::Bool, "BOOL op type mismatch");
          if (!r1.ok) return r1;
          VerifyResult r2 = check_type(b, ValType::Bool, "BOOL op type mismatch");
          if (!r2.ok) return r2;
          push_type(ValType::Bool);
          break;
        }
        case OpCode::JmpTrue:
        case OpCode::JmpFalse: {
          ValType a = pop_type();
          VerifyResult r = check_type(a, ValType::Bool, "JMP type mismatch");
          if (!r.ok) return r;
          break;
        }
        case OpCode::IsNull: {
          ValType a = pop_type();
          VerifyResult r = check_type(a, ValType::Ref, "IS_NULL type mismatch");
          if (!r.ok) return r;
          push_type(ValType::Bool);
          break;
        }
        case OpCode::RefEq:
        case OpCode::RefNe: {
          ValType b = pop_type();
          ValType a = pop_type();
          VerifyResult r1 = check_type(a, ValType::Ref, "REF type mismatch");
          if (!r1.ok) return r1;
          VerifyResult r2 = check_type(b, ValType::Ref, "REF type mismatch");
          if (!r2.ok) return r2;
          push_type(ValType::Bool);
          break;
        }
        case OpCode::TypeOf: {
          ValType a = pop_type();
          VerifyResult r = check_type(a, ValType::Ref, "TYPEOF type mismatch");
          if (!r.ok) return r;
          push_type(ValType::I32);
          break;
        }
        case OpCode::LoadField: {
          ValType a = pop_type();
          VerifyResult r = check_type(a, ValType::Ref, "LOAD_FIELD type mismatch");
          if (!r.ok) return r;
          uint32_t field_id = 0;
          ReadU32(code, pc + 1, &field_id);
          if (field_id >= module.fields.size()) return Fail("LOAD_FIELD bad field id");
          push_type(resolve_type(module.fields[field_id].type_id));
          break;
        }
        case OpCode::StoreField: {
          ValType v = pop_type();
          ValType a = pop_type();
          VerifyResult r1 = check_type(a, ValType::Ref, "STORE_FIELD type mismatch");
          if (!r1.ok) return r1;
          uint32_t field_id = 0;
          ReadU32(code, pc + 1, &field_id);
          if (field_id >= module.fields.size()) return Fail("STORE_FIELD bad field id");
          VerifyResult r2 = check_type(v, resolve_type(module.fields[field_id].type_id), "STORE_FIELD type mismatch");
          if (!r2.ok) return r2;
          break;
        }
        case OpCode::ArrayLen: {
          ValType a = pop_type();
          VerifyResult r = check_type(a, ValType::Ref, "ARRAY_LEN type mismatch");
          if (!r.ok) return r;
          push_type(ValType::I32);
          break;
        }
        case OpCode::ArrayGetI32: {
          ValType idx = pop_type();
          ValType arr = pop_type();
          VerifyResult r1 = check_type(arr, ValType::Ref, "ARRAY_GET type mismatch");
          if (!r1.ok) return r1;
          VerifyResult r2 = check_type(idx, ValType::I32, "ARRAY_GET type mismatch");
          if (!r2.ok) return r2;
          push_type(ValType::I32);
          break;
        }
        case OpCode::ArrayGetI64: {
          ValType idx = pop_type();
          ValType arr = pop_type();
          VerifyResult r1 = check_type(arr, ValType::Ref, "ARRAY_GET type mismatch");
          if (!r1.ok) return r1;
          VerifyResult r2 = check_type(idx, ValType::I32, "ARRAY_GET type mismatch");
          if (!r2.ok) return r2;
          push_type(ValType::I64);
          break;
        }
        case OpCode::ArrayGetF32: {
          ValType idx = pop_type();
          ValType arr = pop_type();
          VerifyResult r1 = check_type(arr, ValType::Ref, "ARRAY_GET type mismatch");
          if (!r1.ok) return r1;
          VerifyResult r2 = check_type(idx, ValType::I32, "ARRAY_GET type mismatch");
          if (!r2.ok) return r2;
          push_type(ValType::F32);
          break;
        }
        case OpCode::ArrayGetF64: {
          ValType idx = pop_type();
          ValType arr = pop_type();
          VerifyResult r1 = check_type(arr, ValType::Ref, "ARRAY_GET type mismatch");
          if (!r1.ok) return r1;
          VerifyResult r2 = check_type(idx, ValType::I32, "ARRAY_GET type mismatch");
          if (!r2.ok) return r2;
          push_type(ValType::F64);
          break;
        }
        case OpCode::ArrayGetRef: {
          ValType idx = pop_type();
          ValType arr = pop_type();
          VerifyResult r1 = check_type(arr, ValType::Ref, "ARRAY_GET type mismatch");
          if (!r1.ok) return r1;
          VerifyResult r2 = check_type(idx, ValType::I32, "ARRAY_GET type mismatch");
          if (!r2.ok) return r2;
          push_type(ValType::Ref);
          break;
        }
        case OpCode::ArraySetI32: {
          ValType value = pop_type();
          ValType idx = pop_type();
          ValType arr = pop_type();
          VerifyResult r1 = check_type(arr, ValType::Ref, "ARRAY_SET type mismatch");
          if (!r1.ok) return r1;
          VerifyResult r2 = check_type(idx, ValType::I32, "ARRAY_SET type mismatch");
          if (!r2.ok) return r2;
          if (value != ValType::Unknown && !is_i32_numeric_type(value)) {
            return fail_at("ARRAY_SET type mismatch", current_pc, current_opcode);
          }
          break;
        }
        case OpCode::ArraySetI64: {
          ValType value = pop_type();
          ValType idx = pop_type();
          ValType arr = pop_type();
          VerifyResult r1 = check_type(arr, ValType::Ref, "ARRAY_SET type mismatch");
          if (!r1.ok) return r1;
          VerifyResult r2 = check_type(idx, ValType::I32, "ARRAY_SET type mismatch");
          if (!r2.ok) return r2;
          VerifyResult r3 = check_type(value, ValType::I64, "ARRAY_SET type mismatch");
          if (!r3.ok) return r3;
          break;
        }
        case OpCode::ArraySetF32: {
          ValType value = pop_type();
          ValType idx = pop_type();
          ValType arr = pop_type();
          VerifyResult r1 = check_type(arr, ValType::Ref, "ARRAY_SET type mismatch");
          if (!r1.ok) return r1;
          VerifyResult r2 = check_type(idx, ValType::I32, "ARRAY_SET type mismatch");
          if (!r2.ok) return r2;
          VerifyResult r3 = check_type(value, ValType::F32, "ARRAY_SET type mismatch");
          if (!r3.ok) return r3;
          break;
        }
        case OpCode::ArraySetF64: {
          ValType value = pop_type();
          ValType idx = pop_type();
          ValType arr = pop_type();
          VerifyResult r1 = check_type(arr, ValType::Ref, "ARRAY_SET type mismatch");
          if (!r1.ok) return r1;
          VerifyResult r2 = check_type(idx, ValType::I32, "ARRAY_SET type mismatch");
          if (!r2.ok) return r2;
          VerifyResult r3 = check_type(value, ValType::F64, "ARRAY_SET type mismatch");
          if (!r3.ok) return r3;
          break;
        }
        case OpCode::ArraySetRef: {
          ValType value = pop_type();
          ValType idx = pop_type();
          ValType arr = pop_type();
          VerifyResult r1 = check_type(arr, ValType::Ref, "ARRAY_SET type mismatch");
          if (!r1.ok) return r1;
          VerifyResult r2 = check_type(idx, ValType::I32, "ARRAY_SET type mismatch");
          if (!r2.ok) return r2;
          VerifyResult r3 = check_type(value, ValType::Ref, "ARRAY_SET type mismatch");
          if (!r3.ok) return r3;
          break;
        }
        case OpCode::ListLen: {
          ValType a = pop_type();
          VerifyResult r = check_type(a, ValType::Ref, "LIST_LEN type mismatch");
          if (!r.ok) return r;
          push_type(ValType::I32);
          break;
        }
        case OpCode::ListGetI32: {
          ValType idx = pop_type();
          ValType list = pop_type();
          VerifyResult r1 = check_type(list, ValType::Ref, "LIST_GET type mismatch");
          if (!r1.ok) return r1;
          VerifyResult r2 = check_type(idx, ValType::I32, "LIST_GET type mismatch");
          if (!r2.ok) return r2;
          push_type(ValType::I32);
          break;
        }
        case OpCode::ListGetI64: {
          ValType idx = pop_type();
          ValType list = pop_type();
          VerifyResult r1 = check_type(list, ValType::Ref, "LIST_GET type mismatch");
          if (!r1.ok) return r1;
          VerifyResult r2 = check_type(idx, ValType::I32, "LIST_GET type mismatch");
          if (!r2.ok) return r2;
          push_type(ValType::I64);
          break;
        }
        case OpCode::ListGetF32: {
          ValType idx = pop_type();
          ValType list = pop_type();
          VerifyResult r1 = check_type(list, ValType::Ref, "LIST_GET type mismatch");
          if (!r1.ok) return r1;
          VerifyResult r2 = check_type(idx, ValType::I32, "LIST_GET type mismatch");
          if (!r2.ok) return r2;
          push_type(ValType::F32);
          break;
        }
        case OpCode::ListGetF64: {
          ValType idx = pop_type();
          ValType list = pop_type();
          VerifyResult r1 = check_type(list, ValType::Ref, "LIST_GET type mismatch");
          if (!r1.ok) return r1;
          VerifyResult r2 = check_type(idx, ValType::I32, "LIST_GET type mismatch");
          if (!r2.ok) return r2;
          push_type(ValType::F64);
          break;
        }
        case OpCode::ListGetRef: {
          ValType idx = pop_type();
          ValType list = pop_type();
          VerifyResult r1 = check_type(list, ValType::Ref, "LIST_GET type mismatch");
          if (!r1.ok) return r1;
          VerifyResult r2 = check_type(idx, ValType::I32, "LIST_GET type mismatch");
          if (!r2.ok) return r2;
          push_type(ValType::Ref);
          break;
        }
        case OpCode::ListSetI32: {
          ValType value = pop_type();
          ValType idx = pop_type();
          ValType list = pop_type();
          VerifyResult r1 = check_type(list, ValType::Ref, "LIST_SET type mismatch");
          if (!r1.ok) return r1;
          VerifyResult r2 = check_type(idx, ValType::I32, "LIST_SET type mismatch");
          if (!r2.ok) return r2;
          if (value != ValType::Unknown && !is_i32_numeric_type(value)) {
            return fail_at("LIST_SET type mismatch", current_pc, current_opcode);
          }
          break;
        }
        case OpCode::ListSetI64: {
          ValType value = pop_type();
          ValType idx = pop_type();
          ValType list = pop_type();
          VerifyResult r1 = check_type(list, ValType::Ref, "LIST_SET type mismatch");
          if (!r1.ok) return r1;
          VerifyResult r2 = check_type(idx, ValType::I32, "LIST_SET type mismatch");
          if (!r2.ok) return r2;
          VerifyResult r3 = check_type(value, ValType::I64, "LIST_SET type mismatch");
          if (!r3.ok) return r3;
          break;
        }
        case OpCode::ListSetF32: {
          ValType value = pop_type();
          ValType idx = pop_type();
          ValType list = pop_type();
          VerifyResult r1 = check_type(list, ValType::Ref, "LIST_SET type mismatch");
          if (!r1.ok) return r1;
          VerifyResult r2 = check_type(idx, ValType::I32, "LIST_SET type mismatch");
          if (!r2.ok) return r2;
          VerifyResult r3 = check_type(value, ValType::F32, "LIST_SET type mismatch");
          if (!r3.ok) return r3;
          break;
        }
        case OpCode::ListSetF64: {
          ValType value = pop_type();
          ValType idx = pop_type();
          ValType list = pop_type();
          VerifyResult r1 = check_type(list, ValType::Ref, "LIST_SET type mismatch");
          if (!r1.ok) return r1;
          VerifyResult r2 = check_type(idx, ValType::I32, "LIST_SET type mismatch");
          if (!r2.ok) return r2;
          VerifyResult r3 = check_type(value, ValType::F64, "LIST_SET type mismatch");
          if (!r3.ok) return r3;
          break;
        }
        case OpCode::ListSetRef: {
          ValType value = pop_type();
          ValType idx = pop_type();
          ValType list = pop_type();
          VerifyResult r1 = check_type(list, ValType::Ref, "LIST_SET type mismatch");
          if (!r1.ok) return r1;
          VerifyResult r2 = check_type(idx, ValType::I32, "LIST_SET type mismatch");
          if (!r2.ok) return r2;
          VerifyResult r3 = check_type(value, ValType::Ref, "LIST_SET type mismatch");
          if (!r3.ok) return r3;
          break;
        }
        case OpCode::ListPushI32: {
          ValType value = pop_type();
          ValType list = pop_type();
          VerifyResult r1 = check_type(list, ValType::Ref, "LIST_PUSH type mismatch");
          if (!r1.ok) return r1;
          if (value != ValType::Unknown && !is_i32_numeric_type(value)) {
            return fail_at("LIST_PUSH type mismatch", current_pc, current_opcode);
          }
          break;
        }
        case OpCode::ListPushI64: {
          ValType value = pop_type();
          ValType list = pop_type();
          VerifyResult r1 = check_type(list, ValType::Ref, "LIST_PUSH type mismatch");
          if (!r1.ok) return r1;
          VerifyResult r2 = check_type(value, ValType::I64, "LIST_PUSH type mismatch");
          if (!r2.ok) return r2;
          break;
        }
        case OpCode::ListPushF32: {
          ValType value = pop_type();
          ValType list = pop_type();
          VerifyResult r1 = check_type(list, ValType::Ref, "LIST_PUSH type mismatch");
          if (!r1.ok) return r1;
          VerifyResult r2 = check_type(value, ValType::F32, "LIST_PUSH type mismatch");
          if (!r2.ok) return r2;
          break;
        }
        case OpCode::ListPushF64: {
          ValType value = pop_type();
          ValType list = pop_type();
          VerifyResult r1 = check_type(list, ValType::Ref, "LIST_PUSH type mismatch");
          if (!r1.ok) return r1;
          VerifyResult r2 = check_type(value, ValType::F64, "LIST_PUSH type mismatch");
          if (!r2.ok) return r2;
          break;
        }
        case OpCode::ListPushRef: {
          ValType value = pop_type();
          ValType list = pop_type();
          VerifyResult r1 = check_type(list, ValType::Ref, "LIST_PUSH type mismatch");
          if (!r1.ok) return r1;
          VerifyResult r2 = check_type(value, ValType::Ref, "LIST_PUSH type mismatch");
          if (!r2.ok) return r2;
          break;
        }
        case OpCode::ListPopI32: {
          ValType list = pop_type();
          VerifyResult r = check_type(list, ValType::Ref, "LIST_POP type mismatch");
          if (!r.ok) return r;
          push_type(ValType::I32);
          break;
        }
        case OpCode::ListPopI64: {
          ValType list = pop_type();
          VerifyResult r = check_type(list, ValType::Ref, "LIST_POP type mismatch");
          if (!r.ok) return r;
          push_type(ValType::I64);
          break;
        }
        case OpCode::ListPopF32: {
          ValType list = pop_type();
          VerifyResult r = check_type(list, ValType::Ref, "LIST_POP type mismatch");
          if (!r.ok) return r;
          push_type(ValType::F32);
          break;
        }
        case OpCode::ListPopF64: {
          ValType list = pop_type();
          VerifyResult r = check_type(list, ValType::Ref, "LIST_POP type mismatch");
          if (!r.ok) return r;
          push_type(ValType::F64);
          break;
        }
        case OpCode::ListPopRef: {
          ValType list = pop_type();
          VerifyResult r = check_type(list, ValType::Ref, "LIST_POP type mismatch");
          if (!r.ok) return r;
          push_type(ValType::Ref);
          break;
        }
        case OpCode::ListInsertI32: {
          ValType value = pop_type();
          ValType idx = pop_type();
          ValType list = pop_type();
          VerifyResult r1 = check_type(list, ValType::Ref, "LIST_INSERT type mismatch");
          if (!r1.ok) return r1;
          VerifyResult r2 = check_type(idx, ValType::I32, "LIST_INSERT type mismatch");
          if (!r2.ok) return r2;
          if (value != ValType::Unknown && !is_i32_numeric_type(value)) {
            return fail_at("LIST_INSERT type mismatch", current_pc, current_opcode);
          }
          break;
        }
        case OpCode::ListInsertI64: {
          ValType value = pop_type();
          ValType idx = pop_type();
          ValType list = pop_type();
          VerifyResult r1 = check_type(list, ValType::Ref, "LIST_INSERT type mismatch");
          if (!r1.ok) return r1;
          VerifyResult r2 = check_type(idx, ValType::I32, "LIST_INSERT type mismatch");
          if (!r2.ok) return r2;
          VerifyResult r3 = check_type(value, ValType::I64, "LIST_INSERT type mismatch");
          if (!r3.ok) return r3;
          break;
        }
        case OpCode::ListInsertF32: {
          ValType value = pop_type();
          ValType idx = pop_type();
          ValType list = pop_type();
          VerifyResult r1 = check_type(list, ValType::Ref, "LIST_INSERT type mismatch");
          if (!r1.ok) return r1;
          VerifyResult r2 = check_type(idx, ValType::I32, "LIST_INSERT type mismatch");
          if (!r2.ok) return r2;
          VerifyResult r3 = check_type(value, ValType::F32, "LIST_INSERT type mismatch");
          if (!r3.ok) return r3;
          break;
        }
        case OpCode::ListInsertF64: {
          ValType value = pop_type();
          ValType idx = pop_type();
          ValType list = pop_type();
          VerifyResult r1 = check_type(list, ValType::Ref, "LIST_INSERT type mismatch");
          if (!r1.ok) return r1;
          VerifyResult r2 = check_type(idx, ValType::I32, "LIST_INSERT type mismatch");
          if (!r2.ok) return r2;
          VerifyResult r3 = check_type(value, ValType::F64, "LIST_INSERT type mismatch");
          if (!r3.ok) return r3;
          break;
        }
        case OpCode::ListInsertRef: {
          ValType value = pop_type();
          ValType idx = pop_type();
          ValType list = pop_type();
          VerifyResult r1 = check_type(list, ValType::Ref, "LIST_INSERT type mismatch");
          if (!r1.ok) return r1;
          VerifyResult r2 = check_type(idx, ValType::I32, "LIST_INSERT type mismatch");
          if (!r2.ok) return r2;
          VerifyResult r3 = check_type(value, ValType::Ref, "LIST_INSERT type mismatch");
          if (!r3.ok) return r3;
          break;
        }
        case OpCode::ListRemoveI32: {
          ValType idx = pop_type();
          ValType list = pop_type();
          VerifyResult r1 = check_type(list, ValType::Ref, "LIST_REMOVE type mismatch");
          if (!r1.ok) return r1;
          VerifyResult r2 = check_type(idx, ValType::I32, "LIST_REMOVE type mismatch");
          if (!r2.ok) return r2;
          push_type(ValType::I32);
          break;
        }
        case OpCode::ListRemoveI64: {
          ValType idx = pop_type();
          ValType list = pop_type();
          VerifyResult r1 = check_type(list, ValType::Ref, "LIST_REMOVE type mismatch");
          if (!r1.ok) return r1;
          VerifyResult r2 = check_type(idx, ValType::I32, "LIST_REMOVE type mismatch");
          if (!r2.ok) return r2;
          push_type(ValType::I64);
          break;
        }
        case OpCode::ListRemoveF32: {
          ValType idx = pop_type();
          ValType list = pop_type();
          VerifyResult r1 = check_type(list, ValType::Ref, "LIST_REMOVE type mismatch");
          if (!r1.ok) return r1;
          VerifyResult r2 = check_type(idx, ValType::I32, "LIST_REMOVE type mismatch");
          if (!r2.ok) return r2;
          push_type(ValType::F32);
          break;
        }
        case OpCode::ListRemoveF64: {
          ValType idx = pop_type();
          ValType list = pop_type();
          VerifyResult r1 = check_type(list, ValType::Ref, "LIST_REMOVE type mismatch");
          if (!r1.ok) return r1;
          VerifyResult r2 = check_type(idx, ValType::I32, "LIST_REMOVE type mismatch");
          if (!r2.ok) return r2;
          push_type(ValType::F64);
          break;
        }
        case OpCode::ListRemoveRef: {
          ValType idx = pop_type();
          ValType list = pop_type();
          VerifyResult r1 = check_type(list, ValType::Ref, "LIST_REMOVE type mismatch");
          if (!r1.ok) return r1;
          VerifyResult r2 = check_type(idx, ValType::I32, "LIST_REMOVE type mismatch");
          if (!r2.ok) return r2;
          push_type(ValType::Ref);
          break;
        }
        case OpCode::ListClear: {
          ValType list = pop_type();
          VerifyResult r = check_type(list, ValType::Ref, "LIST_CLEAR type mismatch");
          if (!r.ok) return r;
          break;
        }
        case OpCode::StringLen: {
          ValType list = pop_type();
          VerifyResult r = check_type(list, ValType::Ref, "STRING_LEN type mismatch");
          if (!r.ok) return r;
          push_type(ValType::I32);
          break;
        }
        case OpCode::StringConcat: {
          ValType b = pop_type();
          ValType a = pop_type();
          VerifyResult r1 = check_type(a, ValType::Ref, "STRING_CONCAT type mismatch");
          if (!r1.ok) return r1;
          VerifyResult r2 = check_type(b, ValType::Ref, "STRING_CONCAT type mismatch");
          if (!r2.ok) return r2;
          push_type(ValType::Ref);
          break;
        }
        case OpCode::StringGetChar: {
          ValType idx = pop_type();
          ValType str = pop_type();
          VerifyResult r1 = check_type(str, ValType::Ref, "STRING_GET_CHAR type mismatch");
          if (!r1.ok) return r1;
          VerifyResult r2 = check_type(idx, ValType::I32, "STRING_GET_CHAR type mismatch");
          if (!r2.ok) return r2;
          push_type(ValType::Char);
          break;
        }
        case OpCode::StringSlice: {
          ValType end_idx = pop_type();
          ValType start_idx = pop_type();
          ValType str = pop_type();
          VerifyResult r1 = check_type(str, ValType::Ref, "STRING_SLICE type mismatch");
          if (!r1.ok) return r1;
          VerifyResult r2 = check_type(start_idx, ValType::I32, "STRING_SLICE type mismatch");
          if (!r2.ok) return r2;
          VerifyResult r3 = check_type(end_idx, ValType::I32, "STRING_SLICE type mismatch");
          if (!r3.ok) return r3;
          push_type(ValType::Ref);
          break;
        }
        case OpCode::Intrinsic: {
          uint32_t id = 0;
          if (!ReadU32(code, pc + 1, &id)) return fail_at("INTRINSIC id out of bounds", pc, opcode);
          if (!IsKnownIntrinsic(id)) return fail_at("INTRINSIC id invalid", pc, opcode);
          IntrinsicSig intrinsic_sig{};
          if (!GetIntrinsicSig(id, &intrinsic_sig)) return fail_at("INTRINSIC signature missing", pc, opcode);
          if (stack_types.size() < intrinsic_sig.param_count) return fail_at("INTRINSIC stack underflow", pc, opcode);
          for (int i = static_cast<int>(intrinsic_sig.param_count) - 1; i >= 0; --i) {
            ValType arg = pop_type();
            ValType expected = from_intrinsic_type(intrinsic_sig.params[static_cast<size_t>(i)]);
            VerifyResult r = check_type(arg, expected, "INTRINSIC param type mismatch");
            if (!r.ok) return r;
          }
          if (intrinsic_sig.ret != 0) {
            ValType ret = from_intrinsic_type(intrinsic_sig.ret);
            push_type(ret);
          }
          extra_pops = static_cast<int>(intrinsic_sig.param_count);
          extra_pushes = (intrinsic_sig.ret != 0) ? 1 : 0;
          break;
        }
        case OpCode::SysCall: {
          uint32_t id = 0;
          if (!ReadU32(code, pc + 1, &id)) return fail_at("SYS_CALL id out of bounds", pc, opcode);
          return fail_at("SYS_CALL not supported", pc, opcode);
        }
        case OpCode::CallCheck:
          if (call_depth != 0) return fail_at("CALLCHECK not in root", pc, opcode);
          break;
        case OpCode::Call: {
          if (pc + 5 >= code.size()) return fail_at("CALL arg count out of bounds", pc, opcode);
          uint8_t arg_count = code[pc + 5];
          if (stack_types.size() < arg_count) return fail_at("CALL stack underflow", pc, opcode);
          uint32_t func_id = 0;
          if (!ReadU32(code, pc + 1, &func_id)) return fail_at("CALL function id out of bounds", pc, opcode);
          if (func_id >= module.functions.size()) return fail_at("CALL function id out of range", pc, opcode);
          uint32_t callee_method = module.functions[func_id].method_id;
          if (callee_method >= module.methods.size()) return fail_at("CALL method id out of range", pc, opcode);
          uint32_t call_sig_id = module.methods[callee_method].sig_id;
          if (call_sig_id >= module.sigs.size()) return fail_at("CALL signature id out of range", pc, opcode);
          const auto& call_sig = module.sigs[call_sig_id];
          if (call_sig.param_count > 0 &&
              call_sig.param_type_start + call_sig.param_count > module.param_types.size()) {
            return fail_at("CALL signature param types out of range", pc, opcode);
          }
          for (int i = static_cast<int>(call_sig.param_count) - 1; i >= 0; --i) {
            ValType got = pop_type();
            ValType expected = ValType::Unknown;
            if (call_sig.param_count > 0) {
              uint32_t type_id = module.param_types[call_sig.param_type_start + static_cast<uint16_t>(i)];
              expected = resolve_type(type_id);
            }
            VerifyResult r = check_type(got, expected, "CALL arg type mismatch");
            if (!r.ok) return r;
          }
          if (call_sig.ret_type_id != 0xFFFFFFFFu) {
            ValType ret_type = resolve_type(call_sig.ret_type_id);
            push_type(ret_type);
            extra_pushes = 1;
          } else {
            extra_pushes = 0;
          }
          extra_pops = arg_count;
          break;
        }
        case OpCode::CallIndirect: {
          if (pc + 5 >= code.size()) return fail_at("CALL_INDIRECT arg count out of bounds", pc, opcode);
          uint8_t arg_count = code[pc + 5];
          if (stack_types.size() < static_cast<size_t>(arg_count) + 1u) {
            return fail_at("CALL_INDIRECT stack underflow", pc, opcode);
          }
          uint32_t indirect_sig_id = 0;
          if (!ReadU32(code, pc + 1, &indirect_sig_id)) return fail_at("CALL_INDIRECT sig id out of bounds", pc, opcode);
          if (indirect_sig_id >= module.sigs.size()) {
            return fail_at("CALL_INDIRECT signature id out of range", pc, opcode);
          }
          const auto& call_sig = module.sigs[indirect_sig_id];
          if (call_sig.param_count > 0 &&
              call_sig.param_type_start + call_sig.param_count > module.param_types.size()) {
            return fail_at("CALL_INDIRECT signature param types out of range", pc, opcode);
          }
          ValType func_type = pop_type();
          if (func_type != ValType::I32 &&
              func_type != ValType::U32 &&
              func_type != ValType::Ref &&
              func_type != ValType::Unknown) {
            return fail_at("CALL_INDIRECT func type mismatch", pc, opcode);
          }
          for (int i = static_cast<int>(call_sig.param_count) - 1; i >= 0; --i) {
            ValType got = pop_type();
            ValType expected = ValType::Unknown;
            if (call_sig.param_count > 0) {
              uint32_t type_id = module.param_types[call_sig.param_type_start + static_cast<uint16_t>(i)];
              expected = resolve_type(type_id);
            }
            VerifyResult rarg = check_type(got, expected, "CALL_INDIRECT arg type mismatch");
            if (!rarg.ok) return rarg;
          }
          if (call_sig.ret_type_id != 0xFFFFFFFFu) {
            ValType ret_type = resolve_type(call_sig.ret_type_id);
            push_type(ret_type);
            extra_pushes = 1;
          } else {
            extra_pushes = 0;
          }
          extra_pops = static_cast<int>(arg_count) + 1;
          break;
        }
        case OpCode::TailCall: {
          if (pc + 5 >= code.size()) return fail_at("TAILCALL arg count out of bounds", pc, opcode);
          uint8_t arg_count = code[pc + 5];
          if (stack_types.size() < arg_count) return fail_at("TAILCALL stack underflow", pc, opcode);
          uint32_t func_id = 0;
          if (!ReadU32(code, pc + 1, &func_id)) return fail_at("TAILCALL function id out of bounds", pc, opcode);
          if (func_id >= module.functions.size()) return fail_at("TAILCALL function id out of range", pc, opcode);
          uint32_t callee_method = module.functions[func_id].method_id;
          if (callee_method >= module.methods.size()) return fail_at("TAILCALL method id out of range", pc, opcode);
          uint32_t tail_sig_id = module.methods[callee_method].sig_id;
          if (tail_sig_id >= module.sigs.size()) return fail_at("TAILCALL signature id out of range", pc, opcode);
          const auto& call_sig = module.sigs[tail_sig_id];
          if (call_sig.param_count > 0 &&
              call_sig.param_type_start + call_sig.param_count > module.param_types.size()) {
            return fail_at("TAILCALL signature param types out of range", pc, opcode);
          }
          for (int i = static_cast<int>(call_sig.param_count) - 1; i >= 0; --i) {
            ValType got = pop_type();
            ValType expected = ValType::Unknown;
            if (call_sig.param_count > 0) {
              uint32_t type_id = module.param_types[call_sig.param_type_start + static_cast<uint16_t>(i)];
              expected = resolve_type(type_id);
            }
            VerifyResult r = check_type(got, expected, "TAILCALL arg type mismatch");
            if (!r.ok) return r;
          }
          extra_pops = arg_count;
          fall_through = false;
          break;
        }
        case OpCode::ConvI32ToI64: {
          ValType v = pop_type();
          if (v != ValType::Unknown && !is_i32_bitwise_type(v) && v != ValType::Bool) {
            return fail_at("CONV type mismatch", pc, opcode);
          }
          push_type(ValType::Unknown);
          break;
        }
        case OpCode::ConvI64ToI32: {
          ValType v = pop_type();
          if (v != ValType::Unknown && v != ValType::I64 && v != ValType::U64) {
            return fail_at("CONV type mismatch", pc, opcode);
          }
          push_type(ValType::Unknown);
          break;
        }
        case OpCode::ConvI32ToF32: {
          ValType v = pop_type();
          if (v != ValType::Unknown && !is_i32_bitwise_type(v) && v != ValType::Bool) {
            return fail_at("CONV type mismatch", pc, opcode);
          }
          push_type(ValType::F32);
          break;
        }
        case OpCode::ConvI32ToF64: {
          ValType v = pop_type();
          if (v != ValType::Unknown && !is_i32_bitwise_type(v) && v != ValType::Bool) {
            return fail_at("CONV type mismatch", pc, opcode);
          }
          push_type(ValType::F64);
          break;
        }
        case OpCode::ConvF32ToI32: {
          ValType v = pop_type();
          VerifyResult r = check_type(v, ValType::F32, "CONV type mismatch");
          if (!r.ok) return r;
          push_type(ValType::Unknown);
          break;
        }
        case OpCode::ConvF64ToI32: {
          ValType v = pop_type();
          VerifyResult r = check_type(v, ValType::F64, "CONV type mismatch");
          if (!r.ok) return r;
          push_type(ValType::Unknown);
          break;
        }
        case OpCode::ConvF32ToF64: {
          ValType v = pop_type();
          VerifyResult r = check_type(v, ValType::F32, "CONV type mismatch");
          if (!r.ok) return r;
          push_type(ValType::F64);
          break;
        }
        case OpCode::ConvF64ToF32: {
          ValType v = pop_type();
          VerifyResult r = check_type(v, ValType::F64, "CONV type mismatch");
          if (!r.ok) return r;
          push_type(ValType::F32);
          break;
        }
        case OpCode::Halt:
        case OpCode::Trap:
        case OpCode::Ret:
          if (static_cast<OpCode>(opcode) == OpCode::Ret) {
            if (expect_void) {
              if (!stack_types.empty()) return fail_at("return value on void", pc, opcode);
            } else {
              if (stack_types.size() != 1) return fail_at("return stack size mismatch", pc, opcode);
              if (expected_ret == ValType::I32) {
                ValType got = stack_types.back();
                if (got != ValType::Unknown && !is_i32_numeric_type(got)) {
                  return fail_at("return type mismatch", pc, opcode);
                }
              } else {
                VerifyResult r = check_type(stack_types.back(), expected_ret, "return type mismatch");
                if (!r.ok) return r;
              }
            }
          }
          fall_through = false;
          break;
        default:
          for (int i = 0; i < info.pops; ++i) pop_type();
          for (int i = 0; i < info.pushes; ++i) push_type(ValType::Unknown);
          break;
      }

      int pop_count = info.pops + extra_pops;
      if (pop_count > 0) {
        if (stack_height - pop_count < 0) return fail_at("stack underflow", pc, opcode);
        stack_height -= pop_count;
      }
      stack_height += info.pushes + extra_pushes;
      if (static_cast<uint32_t>(stack_height) > func.stack_max) {
        return fail_at("stack exceeds max", pc, opcode);
      }
      for (size_t jump_target : jump_targets) {
        auto it = merge_types.find(jump_target);
        if (it == merge_types.end()) {
          merge_types[jump_target] = stack_types;
        } else {
          if (it->second.size() != stack_types.size()) {
            return fail_at("stack merge height mismatch", pc, opcode);
          }
          for (size_t i = 0; i < it->second.size(); ++i) {
            if (it->second[i] == ValType::Unknown) it->second[i] = stack_types[i];
            else if (stack_types[i] != ValType::Unknown && it->second[i] != stack_types[i]) {
              return fail_at("stack merge type mismatch", pc, opcode);
            }
          }
        }
      }

      if (fall_through) {
        auto merge_it = merge_types.find(next);
        if (merge_it != merge_types.end()) {
          if (merge_it->second.size() != stack_types.size()) {
            return fail_at("stack merge height mismatch", pc, opcode);
          }
          for (size_t i = 0; i < stack_types.size(); ++i) {
            if (stack_types[i] == ValType::Unknown) stack_types[i] = merge_it->second[i];
            else if (merge_it->second[i] != ValType::Unknown && merge_it->second[i] != stack_types[i]) {
              return fail_at("stack merge type mismatch", pc, opcode);
            }
          }
        }
      } else {
        auto merge_it = merge_types.find(next);
        if (merge_it != merge_types.end()) {
          stack_types = merge_it->second;
        } else {
          stack_types.clear();
        }
        stack_height = static_cast<int>(stack_types.size());
      }
      pc = next;
    }

    MethodVerifyInfo info;
    info.locals.reserve(locals.size());
    for (ValType t : locals) {
      info.locals.push_back(to_vm_type(t));
    }
    info.locals_ref_bits = make_ref_bits_vm(info.locals);
    info.stack_maps = std::move(stack_maps);
    result.methods[func_index] = std::move(info);
  }

  result.ok = true;
  return result;
}

} // namespace Simple::Byte
