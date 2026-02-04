#include <fstream>
#include <iostream>
#include <sstream>
#include <cstring>

#include "ir_compiler.h"
#include "ir_lang.h"
#include "lang_validate.h"
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

bool WriteFileBytes(const std::string& path,
                    const std::vector<uint8_t>& bytes,
                    std::string* error) {
  std::ofstream out(path, std::ios::binary);
  if (!out) {
    if (error) *error = "failed to open output file";
    return false;
  }
  out.write(reinterpret_cast<const char*>(bytes.data()),
            static_cast<std::streamsize>(bytes.size()));
  if (!out) {
    if (error) *error = "failed to write output file";
    return false;
  }
  return true;
}

bool HasExt(const std::string& path, const char* ext) {
  const size_t len = std::strlen(ext);
  if (path.size() < len) return false;
  return path.rfind(ext) == path.size() - len;
}

std::string ReplaceExt(const std::string& path, const char* ext) {
  const size_t dot = path.find_last_of('.');
  if (dot == std::string::npos) return path + ext;
  return path.substr(0, dot) + ext;
}
} // namespace

int main(int argc, char** argv) {
  if (argc < 2) {
    std::cerr << "usage:\n"
              << "  simplevm run <module.sbc|file.sir|file.simple> [--no-verify]\n"
              << "  simplevm build <file.sir|file.simple> [--out <file.sbc>] [--no-verify]\n"
              << "  simplevm check <file.sbc|file.sir|file.simple>\n"
              << "  simplevm <module.sbc|file.sir|file.simple> [--no-verify]\n";
    return 1;
  }

  const std::string cmd = argv[1];
  const bool is_command = (cmd == "run" || cmd == "build" || cmd == "check");
  const std::string path = is_command ? (argc > 2 ? argv[2] : "") : cmd;
  bool verify = true;
  for (int i = 2; i < argc; ++i) {
    if (std::string(argv[i]) == "--no-verify") {
      verify = false;
    }
  }

  if (is_command && path.empty()) {
    std::cerr << "error: missing input file\n";
    return 1;
  }

  if (cmd == "check") {
    std::string text;
    std::string error;
    if (HasExt(path, ".simple")) {
      if (!ReadFileText(path, &text, &error)) {
        std::cerr << "error: " << error << "\n";
        return 1;
      }
      if (!Simple::Lang::ValidateProgramFromString(text, &error)) {
        std::cerr << "error: " << error << "\n";
        return 1;
      }
      return 0;
    }
    if (HasExt(path, ".sir")) {
      Simple::IR::Text::IrTextModule parsed;
      if (!ReadFileText(path, &text, &error)) {
        std::cerr << "error: " << error << "\n";
        return 1;
      }
      if (!Simple::IR::Text::ParseIrTextModule(text, &parsed, &error)) {
        std::cerr << "error: IR text parse failed (" << path << "): " << error << "\n";
        return 1;
      }
      Simple::IR::IrModule module;
      if (!Simple::IR::Text::LowerIrTextToModule(parsed, &module, &error)) {
        std::cerr << "error: IR text lower failed (" << path << "): " << error << "\n";
        return 1;
      }
      return 0;
    }
    Simple::Byte::LoadResult load = Simple::Byte::LoadModuleFromFile(path);
    if (!load.ok) {
      std::cerr << "error: load failed: " << load.error << "\n";
      return 1;
    }
    Simple::Byte::VerifyResult vr = Simple::Byte::VerifyModule(load.module);
    if (!vr.ok) {
      std::cerr << "error: verify failed: " << vr.error << "\n";
      return 1;
    }
    return 0;
  }

  if (cmd == "build") {
    std::string out_path;
    for (int i = 3; i < argc; ++i) {
      if (std::string(argv[i]) == "--out" && i + 1 < argc) {
        out_path = argv[i + 1];
        ++i;
      }
    }
    if (out_path.empty()) out_path = ReplaceExt(path, ".sbc");

    std::vector<uint8_t> bytes;
    std::string text;
    std::string error;
    if (HasExt(path, ".simple")) {
      if (!ReadFileText(path, &text, &error) || !CompileSimpleToSbc(text, path, &bytes, &error)) {
        std::cerr << "error: " << error << "\n";
        return 1;
      }
    } else if (HasExt(path, ".sir")) {
      if (!ReadFileText(path, &text, &error) || !CompileSirToSbc(text, path, &bytes, &error)) {
        std::cerr << "error: " << error << "\n";
        return 1;
      }
    } else {
      std::cerr << "error: build expects .simple or .sir input\n";
      return 1;
    }
    if (verify) {
      Simple::Byte::LoadResult load = Simple::Byte::LoadModuleFromBytes(bytes);
      if (!load.ok) {
        std::cerr << "error: load failed: " << load.error << "\n";
        return 1;
      }
      Simple::Byte::VerifyResult vr = Simple::Byte::VerifyModule(load.module);
      if (!vr.ok) {
        std::cerr << "error: verify failed: " << vr.error << "\n";
        return 1;
      }
    }
    if (!WriteFileBytes(out_path, bytes, &error)) {
      std::cerr << "error: " << error << "\n";
      return 1;
    }
    return 0;
  }

  if (cmd == "run") {
    if (path.empty()) {
      std::cerr << "error: missing input file\n";
      return 1;
    }
  }

  Simple::Byte::LoadResult load{};
  std::vector<uint8_t> bytes;
  std::string error;
  if (HasExt(path, ".simple")) {
    std::string text;
    if (!ReadFileText(path, &text, &error) || !CompileSimpleToSbc(text, path, &bytes, &error)) {
      std::cerr << "error: " << error << "\n";
      return 1;
    }
    load = Simple::Byte::LoadModuleFromBytes(bytes);
  } else if (HasExt(path, ".sir")) {
    std::string text;
    if (!ReadFileText(path, &text, &error) || !CompileSirToSbc(text, path, &bytes, &error)) {
      std::cerr << "error: " << error << "\n";
      return 1;
    }
    load = Simple::Byte::LoadModuleFromBytes(bytes);
  } else {
    load = Simple::Byte::LoadModuleFromFile(path);
  }
  if (!load.ok) {
    std::cerr << "error: load failed: " << load.error << "\n";
    return 1;
  }

  if (verify) {
    Simple::Byte::VerifyResult vr = Simple::Byte::VerifyModule(load.module);
    if (!vr.ok) {
      std::cerr << "error: verify failed: " << vr.error << "\n";
      return 1;
    }
  }

  Simple::VM::ExecResult exec = Simple::VM::ExecuteModule(load.module, verify);
  if (exec.status == Simple::VM::ExecStatus::Trapped) {
    std::cerr << "error: runtime trap: " << exec.error << "\n";
    return 1;
  }

  return exec.exit_code;
}
