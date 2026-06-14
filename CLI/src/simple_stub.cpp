#include <cstdint>
#include <iostream>
#include <vector>

#include "sbc_loader.h"
#include "sbc_verifier.h"
#include "vm.h"

#ifndef SIMPLE_EMBEDDED_SBC_SIZE
#define SIMPLE_EMBEDDED_SBC_SIZE 0
#endif

#if SIMPLE_EMBEDDED_SBC_SIZE > 0
extern const uint8_t kSimpleEmbeddedSbc[SIMPLE_EMBEDDED_SBC_SIZE];
#else
static const uint8_t kSimpleEmbeddedSbc[1] = {0};
#endif

int main() {
  if constexpr (SIMPLE_EMBEDDED_SBC_SIZE == 0) {
    std::cerr << "simple: no embedded SBC payload\n";
    std::cerr << "build a program with: svm build <file.simple> --out <program>\n";
    return 1;
  } else {
    std::vector<uint8_t> bytes(kSimpleEmbeddedSbc,
                               kSimpleEmbeddedSbc + SIMPLE_EMBEDDED_SBC_SIZE);
    auto load = Simple::Byte::LoadModuleFromBytes(bytes);
    if (!load.ok) {
      std::cerr << "load failed: " << load.error << "\n";
      return 1;
    }
    auto vr = Simple::Byte::VerifyModule(load.module);
    if (!vr.ok) {
      std::cerr << "verify failed: " << vr.error << "\n";
      return 1;
    }
    auto exec = Simple::VM::ExecuteModule(load.module, true);
    if (exec.status == Simple::VM::ExecStatus::Trapped) {
      std::cerr << "runtime trap: " << exec.error << "\n";
      return 1;
    }
    return exec.exit_code;
  }
}
