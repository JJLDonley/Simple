#include <fstream>
#include <iostream>
#include <sstream>

#include "ir_compiler.h"
#include "ir_lang.h"
#include "lang_sir.h"
#include "sbc_loader.h"
#include "sbc_verifier.h"
#include "vm.h"

namespace {
bool ReadFileText(const std::string& path, std::string* out, std::string* error) {
  if (!out) return false;
  std::ifstream in(path);
  if (!in) {
    if (error) *error = "failed to open file";
    return false;
  }
  std::ostringstream buffer;
  buffer << in.rdbuf();
  *out = buffer.str();
  return true;
}

bool CompileSirToSbc(const std::string& text,
                     const std::string& name,
                     std::vector<uint8_t>* out,
                     std::string* error) {
  if (!out) return false;
  Simple::IR::Text::IrTextModule parsed;
  if (!Simple::IR::Text::ParseIrTextModule(text, &parsed, error)) {
    if (error) *error = "IR text parse failed (" + name + "): " + *error;
    return false;
  }
  Simple::IR::IrModule module;
  if (!Simple::IR::Text::LowerIrTextToModule(parsed, &module, error)) {
    if (error) *error = "IR text lower failed (" + name + "): " + *error;
    return false;
  }
  if (!Simple::IR::CompileToSbc(module, out, error)) {
    if (error) *error = "IR compile failed (" + name + "): " + *error;
    return false;
  }
  return true;
}

bool CompileSimpleToSbc(const std::string& text,
                        const std::string& name,
                        std::vector<uint8_t>* out,
                        std::string* error) {
  std::string sir;
  if (!Simple::Lang::EmitSirFromString(text, &sir, error)) {
    if (error) *error = "simple compile failed (" + name + "): " + *error;
    return false;
  }
  return CompileSirToSbc(sir, name, out, error);
}
} // namespace

int main(int argc, char** argv) {
  if (argc < 2) {
    std::cerr << "usage: simplevm <module.sbc|file.sir|file.simple> [--no-verify]\n";
    return 1;
  }

  const std::string path = argv[1];
  bool verify = true;
  if (argc > 2 && std::string(argv[2]) == "--no-verify") {
    verify = false;
  }

  Simple::Byte::LoadResult load{};
  std::vector<uint8_t> bytes;
  std::string error;
  if (path.size() >= 7 && path.rfind(".simple") == path.size() - 7) {
    std::string text;
    if (!ReadFileText(path, &text, &error) || !CompileSimpleToSbc(text, path, &bytes, &error)) {
      std::cerr << error << "\n";
      return 1;
    }
    load = Simple::Byte::LoadModuleFromBytes(bytes);
  } else if (path.size() >= 4 && path.rfind(".sir") == path.size() - 4) {
    std::string text;
    if (!ReadFileText(path, &text, &error) || !CompileSirToSbc(text, path, &bytes, &error)) {
      std::cerr << error << "\n";
      return 1;
    }
    load = Simple::Byte::LoadModuleFromBytes(bytes);
  } else {
    load = Simple::Byte::LoadModuleFromFile(path);
  }
  if (!load.ok) {
    std::cerr << "load failed: " << load.error << "\n";
    return 1;
  }

  if (verify) {
    Simple::Byte::VerifyResult vr = Simple::Byte::VerifyModule(load.module);
    if (!vr.ok) {
      std::cerr << "verify failed: " << vr.error << "\n";
      return 1;
    }
  }

  Simple::VM::ExecResult exec = Simple::VM::ExecuteModule(load.module, verify);
  if (exec.status == Simple::VM::ExecStatus::Trapped) {
    std::cerr << "runtime trap: " << exec.error << "\n";
    return 1;
  }

  return exec.exit_code;
}
