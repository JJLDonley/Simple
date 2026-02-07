#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "opcode.h"
#include "sbc_emitter.h"
#include "sbc_types.h"

namespace {
using namespace Simple::Byte::sbc;

std::vector<uint8_t> BuildAddModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 4);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 5);
  AppendU8(code, static_cast<uint8_t>(OpCode::AddI32));
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));
  return BuildModule(code, 0, 0);
}

std::vector<uint8_t> BuildLoopModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::StoreLocal));
  AppendU32(code, 0);

  size_t loop_start = code.size();
  AppendU8(code, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 3);
  AppendU8(code, static_cast<uint8_t>(OpCode::CmpLtI32));
  AppendU8(code, static_cast<uint8_t>(OpCode::JmpFalse));
  size_t jmp_exit_offset = code.size();
  AppendI32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(code, 1);
  AppendU8(code, static_cast<uint8_t>(OpCode::AddI32));
  AppendU8(code, static_cast<uint8_t>(OpCode::StoreLocal));
  AppendU32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::Jmp));
  size_t jmp_back_offset = code.size();
  AppendI32(code, 0);
  size_t loop_end = code.size();
  AppendU8(code, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));

  int32_t exit_rel = static_cast<int32_t>(loop_end) - static_cast<int32_t>(jmp_exit_offset + 4);
  WriteU32(code, jmp_exit_offset, static_cast<uint32_t>(exit_rel));
  int32_t back_rel = static_cast<int32_t>(loop_start) - static_cast<int32_t>(jmp_back_offset + 4);
  WriteU32(code, jmp_back_offset, static_cast<uint32_t>(back_rel));
  return BuildModule(code, 0, 1);
}

std::vector<uint8_t> BuildFibIterModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> entry;
  AppendU8(entry, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(entry, 0);
  AppendU8(entry, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(entry, 10);
  AppendU8(entry, static_cast<uint8_t>(OpCode::Call));
  AppendU32(entry, 1);
  AppendU8(entry, 1);
  AppendU8(entry, static_cast<uint8_t>(OpCode::Ret));

  std::vector<uint8_t> fib;
  AppendU8(fib, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(fib, 5);
  AppendU8(fib, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(fib, 0);
  AppendU8(fib, static_cast<uint8_t>(OpCode::StoreLocal));
  AppendU32(fib, 1);
  AppendU8(fib, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(fib, 1);
  AppendU8(fib, static_cast<uint8_t>(OpCode::StoreLocal));
  AppendU32(fib, 2);
  AppendU8(fib, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(fib, 0);
  AppendU8(fib, static_cast<uint8_t>(OpCode::StoreLocal));
  AppendU32(fib, 3);

  size_t loop_start = fib.size();
  AppendU8(fib, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(fib, 3);
  AppendU8(fib, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(fib, 0);
  AppendU8(fib, static_cast<uint8_t>(OpCode::CmpLtI32));
  AppendU8(fib, static_cast<uint8_t>(OpCode::JmpFalse));
  size_t jmp_exit_offset = fib.size();
  AppendI32(fib, 0);

  AppendU8(fib, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(fib, 1);
  AppendU8(fib, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(fib, 2);
  AppendU8(fib, static_cast<uint8_t>(OpCode::AddI32));
  AppendU8(fib, static_cast<uint8_t>(OpCode::StoreLocal));
  AppendU32(fib, 4);

  AppendU8(fib, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(fib, 2);
  AppendU8(fib, static_cast<uint8_t>(OpCode::StoreLocal));
  AppendU32(fib, 1);

  AppendU8(fib, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(fib, 4);
  AppendU8(fib, static_cast<uint8_t>(OpCode::StoreLocal));
  AppendU32(fib, 2);

  AppendU8(fib, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(fib, 3);
  AppendU8(fib, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(fib, 1);
  AppendU8(fib, static_cast<uint8_t>(OpCode::AddI32));
  AppendU8(fib, static_cast<uint8_t>(OpCode::StoreLocal));
  AppendU32(fib, 3);

  AppendU8(fib, static_cast<uint8_t>(OpCode::Jmp));
  size_t jmp_back_offset = fib.size();
  AppendI32(fib, 0);

  size_t loop_end = fib.size();
  AppendU8(fib, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(fib, 1);
  AppendU8(fib, static_cast<uint8_t>(OpCode::Ret));

  int32_t exit_rel = static_cast<int32_t>(loop_end) - static_cast<int32_t>(jmp_exit_offset + 4);
  WriteU32(fib, jmp_exit_offset, static_cast<uint32_t>(exit_rel));
  int32_t back_rel = static_cast<int32_t>(loop_start) - static_cast<int32_t>(jmp_back_offset + 4);
  WriteU32(fib, jmp_back_offset, static_cast<uint32_t>(back_rel));

  SigSpec entry_sig{0, 0, {}};
  SigSpec fib_sig{0, 1, {0}};
  std::vector<std::vector<uint8_t>> funcs{entry, fib};
  std::vector<uint16_t> locals{0, 5};
  std::vector<uint32_t> sig_ids{0, 1};
  return BuildModuleWithFunctionsAndSigs(funcs, locals, sig_ids, {entry_sig, fib_sig});
}

std::vector<uint8_t> BuildFibRecModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> entry;
  AppendU8(entry, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(entry, 0);
  AppendU8(entry, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(entry, 5);
  AppendU8(entry, static_cast<uint8_t>(OpCode::Call));
  AppendU32(entry, 1);
  AppendU8(entry, 1);
  AppendU8(entry, static_cast<uint8_t>(OpCode::Ret));

  std::vector<uint8_t> fib;
  AppendU8(fib, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(fib, 1);
  AppendU8(fib, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(fib, 0);
  AppendU8(fib, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(fib, 2);
  AppendU8(fib, static_cast<uint8_t>(OpCode::CmpLtI32));
  AppendU8(fib, static_cast<uint8_t>(OpCode::JmpFalse));
  size_t jmp_else = fib.size();
  AppendI32(fib, 0);
  AppendU8(fib, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(fib, 0);
  AppendU8(fib, static_cast<uint8_t>(OpCode::Ret));

  size_t else_pos = fib.size();
  AppendU8(fib, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(fib, 0);
  AppendU8(fib, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(fib, 1);
  AppendU8(fib, static_cast<uint8_t>(OpCode::SubI32));
  AppendU8(fib, static_cast<uint8_t>(OpCode::Call));
  AppendU32(fib, 1);
  AppendU8(fib, 1);
  AppendU8(fib, static_cast<uint8_t>(OpCode::LoadLocal));
  AppendU32(fib, 0);
  AppendU8(fib, static_cast<uint8_t>(OpCode::ConstI32));
  AppendI32(fib, 2);
  AppendU8(fib, static_cast<uint8_t>(OpCode::SubI32));
  AppendU8(fib, static_cast<uint8_t>(OpCode::Call));
  AppendU32(fib, 1);
  AppendU8(fib, 1);
  AppendU8(fib, static_cast<uint8_t>(OpCode::AddI32));
  AppendU8(fib, static_cast<uint8_t>(OpCode::Ret));

  int32_t rel = static_cast<int32_t>(else_pos) - static_cast<int32_t>(jmp_else + 4);
  WriteU32(fib, jmp_else, static_cast<uint32_t>(rel));

  SigSpec entry_sig{0, 0, {}};
  SigSpec fib_sig{0, 1, {0}};
  std::vector<std::vector<uint8_t>> funcs{entry, fib};
  std::vector<uint16_t> locals{0, 1};
  std::vector<uint32_t> sig_ids{0, 1};
  return BuildModuleWithFunctionsAndSigs(funcs, locals, sig_ids, {entry_sig, fib_sig});
}

std::vector<uint8_t> BuildUuidLenModule() {
  using Simple::Byte::OpCode;
  std::vector<uint8_t> const_pool;
  uint32_t uuid_str_offset = static_cast<uint32_t>(
      AppendStringToPool(const_pool, "123e4567-e89b-12d3-a456-426614174000"));
  uint32_t uuid_const_id = 0;
  AppendConstString(const_pool, uuid_str_offset, &uuid_const_id);

  std::vector<uint8_t> code;
  AppendU8(code, static_cast<uint8_t>(OpCode::Enter));
  AppendU16(code, 0);
  AppendU8(code, static_cast<uint8_t>(OpCode::ConstString));
  AppendU32(code, uuid_const_id);
  AppendU8(code, static_cast<uint8_t>(OpCode::StringLen));
  AppendU8(code, static_cast<uint8_t>(OpCode::Ret));

  std::vector<uint8_t> empty;
  std::vector<uint32_t> no_params;
  return BuildModuleWithTablesAndSig(code, const_pool, empty, empty, 0, 0, 0, 0, 0, 0, no_params);
}

bool WriteFile(const std::string& path, const std::vector<uint8_t>& bytes) {
  std::ofstream out(path, std::ios::binary);
  if (!out) return false;
  out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
  return static_cast<bool>(out);
}

} // namespace

int main(int argc, char** argv) {
  if (argc < 2) {
    std::cerr << "usage: gen_sbc <out.sbc> [--loop|--fib-iter|--fib-rec|--uuid]\n";
    return 1;
  }
  const std::string out_path = argv[1];
  std::string mode;
  if (argc > 2) {
    mode = argv[2];
  }

  std::vector<uint8_t> module;
  if (mode == "--loop") {
    module = BuildLoopModule();
  } else if (mode == "--fib-iter") {
    module = BuildFibIterModule();
  } else if (mode == "--fib-rec") {
    module = BuildFibRecModule();
  } else if (mode == "--uuid") {
    module = BuildUuidLenModule();
  } else {
    module = BuildAddModule();
  }
  if (!WriteFile(out_path, module)) {
    std::cerr << "failed to write: " << out_path << "\n";
    return 1;
  }
  std::cout << "wrote: " << out_path << " (" << module.size() << " bytes)\n";
  return 0;
}
