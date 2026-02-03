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

  Simple::Byte::LoadResult load = Simple::Byte::LoadModuleFromFile(path);
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return 1;
  }

  Simple::VM::ExecResult exec = Simple::VM::ExecuteModule(load.module, verify);
  if (exec.status == Simple::VM::ExecStatus::Trapped) {
    std::cerr << "runtime trap: " << exec.error << "\n";
    return 1;
  }

  return exec.exit_code;
}
