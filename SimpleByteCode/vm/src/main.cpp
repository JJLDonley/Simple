#include <iostream>

#include "sbc_loader.h"
#include "sbc_verifier.h"
#include "vm.h"

int main(int argc, char** argv) {
  if (argc < 2) {
    std::cerr << "usage: simplevm <module.sbc> [--no-verify]\n";
    return 1;
  }

  const std::string path = argv[1];
  bool verify = true;
  if (argc > 2 && std::string(argv[2]) == "--no-verify") {
    verify = false;
  }

  simplevm::LoadResult load = simplevm::LoadModuleFromFile(path);
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return 1;
  }

  if (verify) {
    simplevm::VerifyResult vr = simplevm::VerifyModule(load.module);
    if (!vr.ok) {
      std::cerr << "verify failed: " << vr.error << "\n";
      return 1;
    }
  }

  simplevm::ExecResult exec = simplevm::ExecuteModule(load.module);
  if (exec.status == simplevm::ExecStatus::Trapped) {
    std::cerr << "runtime trap: " << exec.error << "\n";
    return 1;
  }

  return exec.exit_code;
}
