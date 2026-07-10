#include <fstream>
#include <iostream>
#include <sstream>
#include <cstring>
#include <filesystem>
#include <cstdio>
#include <cstdlib>
#include <cinttypes>
#include <algorithm>
#include <cctype>
#include <vector>
#include <unordered_set>
#if defined(__linux__)
#include <unistd.h>
#endif

#include "ir_compiler.h"
#include "ir_lang.h"
#include "CAST/parser.h"
#include "lang_reserved.h"
#include "TAST/type_checker.h"
#include "IRE/sir_emitter.h"
#include "lsp_server.h"
#include "sbc_loader.h"
#include "sbc_verifier.h"
#include "vm.h"
#include "jit/llvm_backend.h"
#include "build_contract.h"
#include "command_contract.h"
#include "command_dispatch.h"
#include "diagnostic_render.h"
#include "RAST/import_loader.h"
#include "import_contract.h"

namespace {
#ifndef SIMPLEVM_VERSION
#define SIMPLEVM_VERSION "v0.4.37"
#endif

const char* ToolVersion() { return SIMPLEVM_VERSION; }

const char* JitTierName(Simple::VM::JitTier tier) {
  switch (tier) {
    case Simple::VM::JitTier::None: return "none";
    case Simple::VM::JitTier::Tier0: return "tier0";
    case Simple::VM::JitTier::Tier1: return "tier1";
  }
  return "unknown";
}

std::string JitFunctionName(const Simple::Byte::SbcModule& module, size_t index) {
  if (index >= module.functions.size()) return {};
  for (const auto& export_row : module.exports) {
    if (export_row.func_id == index && export_row.symbol_name_str < module.const_pool.size()) {
      return Simple::Byte::ReadConstPoolString(module, export_row.symbol_name_str);
    }
  }
  const uint32_t method_id = module.functions[index].method_id;
  if (method_id >= module.methods.size()) return {};
  for (const auto& sym : module.debug_syms) {
    if (sym.kind == 5 && sym.symbol_id == method_id && sym.name_str < module.const_pool.size()) {
      return Simple::Byte::ReadConstPoolString(module, sym.name_str);
    }
  }
  const uint32_t name_offset = module.methods[method_id].name_str;
  if (name_offset == 0 || name_offset >= module.const_pool.size()) return {};
  return Simple::Byte::ReadConstPoolString(module, name_offset);
}

bool ReadFileText(const std::string& path, std::string* out, std::string* error) {
  if (!out) return false;
  std::ifstream in(path);
  if (!in) {
    if (error) *error = "failed to open file: " + path;
    return false;
  }
  std::ostringstream buffer;
  buffer << in.rdbuf();
  *out = buffer.str();
  return true;
}

bool ValidateSimpleFile(const std::string& path, std::string* error) {
  Simple::Lang::Program program;
  if (!Simple::Lang::RAST::LoadProgramWithImports(path, &program, error)) return false;
  return Simple::Lang::ValidateProgram(program, error);
}

bool EmitSirFromSimpleFile(const std::string& path, std::string* out, std::string* error) {
  Simple::Lang::Program program;
  if (!Simple::Lang::RAST::LoadProgramWithImports(path, &program, error)) return false;
  return Simple::Lang::IRE::EmitSir(program, out, error);
}

bool CompileSirToSbc(const std::string& text,
                     const std::string& name,
                     std::vector<uint8_t>* out,
                     std::string* error,
                     bool emit_method_names = false) {
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
  module.emit_method_names = emit_method_names;
  if (!Simple::IR::CompileToSbc(module, out, error)) {
    if (error) *error = "IR compile failed (" + name + "): " + *error;
    return false;
  }
  return true;
}

bool CompileSimpleFileToSbc(const std::string& path,
                            std::vector<uint8_t>* out,
                            std::string* error) {
  std::string sir;
  if (!EmitSirFromSimpleFile(path, &sir, error)) {
    if (error) *error = "simple compile failed (" + path + "): " + *error;
    return false;
  }
  return CompileSirToSbc(sir, path, out, error, true);
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
  return Simple::CLI::HasExtension(path, ext);
}

std::string ReplaceExt(const std::string& path, const char* ext) {
  return Simple::CLI::ReplaceExtension(path, ext);
}

std::string ResolveImplicitSimplePath(const std::string& path) {
  if (path.empty()) return path;
  namespace fs = std::filesystem;
  fs::path p(path);
  if (p.has_extension()) return path;
  fs::path with_ext = p;
  with_ext += ".simple";
  std::error_code ec;
  if (fs::exists(with_ext, ec)) return with_ext.string();
  return path;
}

std::string BaseName(const char* argv0) {
  if (!argv0 || !*argv0) return "simplevm";
  const std::filesystem::path p(argv0);
  std::string name = p.filename().string();
  if (name.empty()) return "simplevm";
  return name;
}

} // namespace

struct ErrorLocation {
  bool ok = false;
  uint32_t line = 0;
  uint32_t column = 0;
  std::string file;
  std::string message;
};

std::string TrimCopy(std::string s) {
  while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) s.erase(s.begin());
  while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.pop_back();
  return s;
}

std::string StripDiagnosticWrappers(const std::string& message,
                                    const std::string& default_path) {
  std::string out = TrimCopy(message);
  for (;;) {
    bool changed = false;
    const std::string compile_prefix = "simple compile failed (";
    if (out.rfind(compile_prefix, 0) == 0) {
      const size_t close = out.find("): ");
      if (close != std::string::npos) {
        out = out.substr(close + 3);
        out = TrimCopy(out);
        changed = true;
      }
    }
    if (!default_path.empty()) {
      const std::string path_prefix = default_path + ": ";
      if (out.rfind(path_prefix, 0) == 0) {
        out = TrimCopy(out.substr(path_prefix.size()));
        changed = true;
      }
    }
    if (!changed) break;
  }
  return out;
}

ErrorLocation ParseErrorLocation(const std::string& raw_message) {
  ErrorLocation out;
  const std::string message = TrimCopy(raw_message);
  for (size_t i = 0; i < message.size(); ++i) {
    if (!std::isdigit(static_cast<unsigned char>(message[i]))) continue;
    size_t p = i;
    while (p < message.size() && std::isdigit(static_cast<unsigned char>(message[p]))) ++p;
    if (p == i || p >= message.size() || message[p] != ':') continue;
    uint32_t line = static_cast<uint32_t>(std::stoul(message.substr(i, p - i)));
    ++p;
    while (p < message.size() && std::isspace(static_cast<unsigned char>(message[p]))) ++p;
    size_t col_start = p;
    while (p < message.size() && std::isdigit(static_cast<unsigned char>(message[p]))) ++p;
    if (p == col_start || p >= message.size() || message[p] != ':') continue;
    uint32_t col = static_cast<uint32_t>(std::stoul(message.substr(col_start, p - col_start)));
    ++p;
    while (p < message.size() && std::isspace(static_cast<unsigned char>(message[p]))) ++p;
    if (line == 0 || col == 0) continue;

    std::string before = TrimCopy(message.substr(0, i));
    while (!before.empty() && (before.back() == ':' || std::isspace(static_cast<unsigned char>(before.back())))) {
      before.pop_back();
    }
    std::string after = p < message.size() ? TrimCopy(message.substr(p)) : std::string("diagnostic error");

    out.ok = true;
    out.line = line;
    out.column = col;

    if (!before.empty()) {
      const bool maybe_path = std::filesystem::path(before).has_parent_path() ||
                              before.find(".simple") != std::string::npos;
      if (maybe_path) {
        out.file = before;
        out.message = after;
      } else {
        out.message = before + ": " + after;
      }
    } else {
      out.message = after;
    }
    return out;
  }

  out.message = message;
  return out;
}

std::string GetSourceLine(const std::string& path, uint32_t line) {
  if (line == 0) return {};
  std::ifstream in(path);
  if (!in) return {};
  std::string text;
  uint32_t current = 0;
  while (std::getline(in, text)) {
    ++current;
    if (current == line) return text;
  }
  return {};
}

void PrintError(const std::string& message) {
  std::cerr << Simple::CLI::RenderErrorLine(message) << "\n";
}

void PrintDiagnosticHelp(const std::string& message) {
  const std::string hint = Simple::CLI::DiagnosticHelpFor(message);
  if (!hint.empty()) {
    std::cerr << "  = help: " << hint << "\n";
  }
}

void PrintErrorWithContext(const std::string& path, const std::string& message) {
  const std::string normalized = StripDiagnosticWrappers(message, path);
  ErrorLocation loc = ParseErrorLocation(normalized);
  if (!loc.ok) {
    PrintError(normalized);
    PrintDiagnosticHelp(normalized);
    return;
  }
  std::cerr << Simple::CLI::RenderErrorLine(loc.message) << "\n";
  const std::string source_path = loc.file.empty() ? path : loc.file;
  std::cerr << " --> " << source_path << ":" << loc.line << ":" << loc.column << "\n";
  std::string source = GetSourceLine(source_path, loc.line);
  if (!source.empty()) {
    std::cerr << "  |\n";
    std::cerr << loc.line << " | " << source << "\n";
    std::cerr << "  | ";
    for (uint32_t i = 1; i < loc.column; ++i) {
      std::cerr << ' ';
    }
    std::cerr << "^\n";
  }
  PrintDiagnosticHelp(loc.message);
}

int main(int argc, char** argv) {
  const std::string tool_name = BaseName(argv[0]);
  const Simple::CLI::ToolMode tool_mode = Simple::CLI::DetectToolMode(tool_name);
  const bool simple_only = tool_mode.simple_only;
  const bool compiler_frontend = tool_mode.compiler_frontend;
  auto print_usage = [&]() {
    std::cerr << "usage:\n";
    if (simple_only) {
      std::cerr << "  " << tool_name << " --version | -v\n"
                << "  " << tool_name << " --help | -h\n"
                << "  " << tool_name << " help\n"
                << "  " << tool_name << " run <module.sbc> [-jit|-int] [--jit-stats] [--no-verify]\n"
                << "  " << tool_name << " <module.sbc> [-jit|-int] [--jit-stats] [--no-verify]\n";
    } else if (compiler_frontend) {
      std::cerr << "  " << tool_name << " --version | -v\n"
                << "  " << tool_name << " --help | -h\n"
                << "  " << tool_name << " help\n"
                << "  " << tool_name << " run <module.sbc|file.sir|file.simple> [-jit|-int] [--jit-stats] [--no-verify]\n"
                << "  " << tool_name
                << " build <file.simple|file.sir> [--out <file.exe|file.sbc>] [-d|--dynamic|-s|--static] [--no-verify]\n"
                << "  " << tool_name
                << " compile <file.simple|file.sir> [--out <file.exe|file.sbc>] [-d|--dynamic|-s|--static] [--no-verify]\n"
                << "  " << tool_name << " emit -ir <file.simple> [--out <file.sir>]\n"
                << "  " << tool_name << " emit -sbc <file.sir|file.simple> [--out <file.sbc>] [--no-verify]\n"
                << "  " << tool_name << " check <file.sbc|file.sir|file.simple>\n"
                << "  " << tool_name << " lsp\n"
                << "  " << tool_name << " <module.sbc|file.sir|file.simple> [-jit|-int] [--jit-stats] [--no-verify]\n";
    } else {
      std::cerr << "  " << tool_name << " --version | -v\n"
                << "  " << tool_name << " --help | -h\n"
                << "  " << tool_name << " help\n";
    }
  };
  if (argc < 2) {
    print_usage();
    return 1;
  }

  if (std::string(argv[1]) == "--version" || std::string(argv[1]) == "-v" ||
      std::string(argv[1]) == "version") {
    Simple::VM::Jit::LlvmJitStatus llvm_status = Simple::VM::Jit::GetLlvmJitStatus();
    std::cout << tool_name << " " << ToolVersion() << "\n"
              << "llvm-jit: " << (llvm_status.available ? "on" : "off")
              << " (" << llvm_status.message << ")\n";
    return 0;
  }

  if (std::string(argv[1]) == "--help" || std::string(argv[1]) == "-h" ||
      std::string(argv[1]) == "help") {
    print_usage();
    return 0;
  }

  const std::string cmd = argv[1];
  const bool build_cmd = Simple::CLI::IsBuildCommand(cmd);
  const bool is_command = Simple::CLI::IsKnownCommand(cmd);
  std::string path = ResolveImplicitSimplePath(is_command ? (argc > 2 ? argv[2] : "") : cmd);
  bool verify = true;
  bool use_jit = false;
  bool force_interpreter = true;
  bool build_exe = false;
  bool build_static = false;
  bool print_jit_stats = false;
  bool build_mode_explicit = false;
  for (int i = 2; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "--no-verify") {
      verify = false;
    } else if (arg == "--jit-stats" || arg == "--jit-trace") {
      print_jit_stats = true;
    } else if (arg == "-jit" || arg == "--jit" || arg == "--llvm-jit") {
      use_jit = true;
      force_interpreter = false;
    } else if (arg == "-int" || arg == "--int" || arg == "--interpreter") {
      use_jit = false;
      force_interpreter = true;
    } else if (arg == "-d" || arg == "--dynamic") {
      build_exe = true;
      build_static = false;
      build_mode_explicit = true;
    } else if (arg == "-s" || arg == "--static") {
      build_exe = true;
      build_static = true;
      build_mode_explicit = true;
    } else if (is_command && cmd != "emit" && arg != "--out" && !arg.empty() && arg[0] != '-' &&
               (path.empty() || (!path.empty() && path[0] == '-'))) {
      path = ResolveImplicitSimplePath(arg);
    }
  }

  if (is_command && cmd != "lsp" && path.empty()) {
    PrintError("missing input file");
    return 1;
  }

  if (simple_only && is_command && cmd != "run") {
    PrintError("simple is a runtime stub; use svm for compiler commands");
    return 1;
  }

  if (cmd == "lsp") {
    return Simple::LSP::RunServer(std::cin, std::cout);
  }

  if (cmd == "check") {
    if (simple_only && !HasExt(path, ".simple")) {
      PrintError("simple expects .simple input");
      return 1;
    }
    std::string text;
    std::string error;
    if (HasExt(path, ".simple")) {
      if (!ValidateSimpleFile(path, &error)) {
        PrintErrorWithContext(path, error);
        return 1;
      }
      return 0;
    }
    if (HasExt(path, ".sir")) {
      Simple::IR::Text::IrTextModule parsed;
      if (!ReadFileText(path, &text, &error)) {
        PrintError(error);
        return 1;
      }
      if (!Simple::IR::Text::ParseIrTextModule(text, &parsed, &error)) {
        PrintError("IR text parse failed (" + path + "): " + error);
        return 1;
      }
      Simple::IR::IrModule module;
      if (!Simple::IR::Text::LowerIrTextToModule(parsed, &module, &error)) {
        PrintError("IR text lower failed (" + path + "): " + error);
        return 1;
      }
      return 0;
    }
    Simple::Byte::LoadResult load = Simple::Byte::LoadModuleFromFile(path);
    if (!load.ok) {
      PrintError("load failed: " + load.error);
      return 1;
    }
    Simple::Byte::VerifyResult vr = Simple::Byte::VerifyModule(load.module);
    if (!vr.ok) {
      PrintError("verify failed: " + vr.error);
      return 1;
    }
    return 0;
  }

  if (cmd == "emit") {
    if (argc < 4) {
      PrintError("emit expects -ir or -sbc and an input file");
      return 1;
    }
    const std::string mode = argv[2];
    const std::string emit_path = ResolveImplicitSimplePath(argv[3]);
    if (simple_only && !HasExt(emit_path, ".simple")) {
      PrintError("simple expects .simple input");
      return 1;
    }
    std::string out_path;
    for (int i = 4; i < argc; ++i) {
      if (std::string(argv[i]) == "--out" && i + 1 < argc) {
        out_path = argv[i + 1];
        ++i;
      }
    }

    std::string text;
    std::string error;
    if (mode == "-ir") {
      if (!HasExt(emit_path, ".simple")) {
        PrintError("emit -ir expects .simple input");
        return 1;
      }
      if (out_path.empty()) out_path = ReplaceExt(emit_path, ".sir");
      std::string sir;
      if (!EmitSirFromSimpleFile(emit_path, &sir, &error)) {
        PrintErrorWithContext(emit_path, "simple compile failed (" + emit_path + "): " + error);
        return 1;
      }
      std::vector<uint8_t> bytes(sir.begin(), sir.end());
      if (!WriteFileBytes(out_path, bytes, &error)) {
        PrintError(error);
        return 1;
      }
      return 0;
    }
    if (mode == "-sbc") {
      if (out_path.empty()) out_path = ReplaceExt(emit_path, ".sbc");
      std::vector<uint8_t> bytes;
      if (HasExt(emit_path, ".simple")) {
        if (!CompileSimpleFileToSbc(emit_path, &bytes, &error)) {
          PrintErrorWithContext(emit_path, error);
          return 1;
        }
      } else if (HasExt(emit_path, ".sir")) {
        if (!ReadFileText(emit_path, &text, &error) ||
            !CompileSirToSbc(text, emit_path, &bytes, &error)) {
          PrintError(error);
          return 1;
        }
      } else {
        PrintError("emit -sbc expects .simple or .sir input");
        return 1;
      }
      if (verify) {
        Simple::Byte::LoadResult load = Simple::Byte::LoadModuleFromBytes(bytes);
        if (!load.ok) {
          PrintError("load failed: " + load.error);
          return 1;
        }
        Simple::Byte::VerifyResult vr = Simple::Byte::VerifyModule(load.module);
        if (!vr.ok) {
          PrintError("verify failed: " + vr.error);
          return 1;
        }
      }
      if (!WriteFileBytes(out_path, bytes, &error)) {
        PrintError(error);
        return 1;
      }
      return 0;
    }
    PrintError("emit expects -ir or -sbc");
    return 1;
  }

  if (build_cmd) {
    std::string input_path = path;
    if (input_path.empty() || (!input_path.empty() && input_path[0] == '-')) {
      for (int i = 2; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--out") {
          ++i;
          continue;
        }
        if (arg == "--no-verify" || arg == "-d" || arg == "--dynamic" || arg == "-s" || arg == "--static") {
          continue;
        }
        if (!arg.empty() && arg[0] != '-') {
          input_path = arg;
          break;
        }
      }
    }
    if (input_path.empty()) {
      PrintError("missing input file");
      return 1;
    }
    input_path = ResolveImplicitSimplePath(input_path);
    if (simple_only && !HasExt(input_path, ".simple")) {
      PrintError("simple expects .simple input");
      return 1;
    }

    std::string out_path;
    for (int i = 3; i < argc; ++i) {
      if (std::string(argv[i]) == "--out" && i + 1 < argc) {
        out_path = argv[i + 1];
        ++i;
      }
    }
    if (!build_mode_explicit && compiler_frontend && (out_path.empty() || !HasExt(out_path, ".sbc"))) {
      build_exe = true;
    }
    if (out_path.empty()) {
      out_path = Simple::CLI::DefaultBuildOutputPath(input_path, build_exe);
    }

    std::vector<uint8_t> bytes;
    std::string text;
    std::string error;
    if (HasExt(input_path, ".simple")) {
      if (!CompileSimpleFileToSbc(input_path, &bytes, &error)) {
        PrintErrorWithContext(input_path, error);
        return 1;
      }
    } else if (HasExt(input_path, ".sir")) {
      if (!ReadFileText(input_path, &text, &error) ||
          !CompileSirToSbc(text, input_path, &bytes, &error)) {
        PrintError(error);
        return 1;
      }
    } else {
      PrintError("build expects .simple or .sir input");
      return 1;
    }
    if (verify) {
      Simple::Byte::LoadResult load = Simple::Byte::LoadModuleFromBytes(bytes);
      if (!load.ok) {
        PrintError("load failed: " + load.error);
        return 1;
      }
      Simple::Byte::VerifyResult vr = Simple::Byte::VerifyModule(load.module);
      if (!vr.ok) {
        PrintError("verify failed: " + vr.error);
        return 1;
      }
    }
    if (build_exe) {
      Simple::CLI::BuildLayoutPaths layout;
      if (!Simple::CLI::ResolveBuildLayoutPaths(argv[0], &layout)) {
        PrintError("unable to resolve runtime/include paths; install simple runtime or run from source tree");
        return 1;
      }
      if (!Simple::CLI::BuildEmbeddedExecutable(layout, bytes, out_path, build_static, &error)) {
        PrintError(error);
        return 1;
      }
    } else {
      if (!WriteFileBytes(out_path, bytes, &error)) {
        PrintError(error);
        return 1;
      }
    }
    return 0;
  }

  if (cmd == "run") {
    if (path.empty()) {
      PrintError("missing input file");
      return 1;
    }
    if (simple_only && !HasExt(path, ".sbc")) {
      PrintError("simple expects an embedded payload or .sbc input");
      return 1;
    }
  }

  Simple::Byte::LoadResult load{};
  std::vector<uint8_t> bytes;
  std::string error;
  if (simple_only && !HasExt(path, ".sbc")) {
    PrintError("simple expects an embedded payload or .sbc input");
    return 1;
  }
  if (HasExt(path, ".simple")) {
    if (!CompileSimpleFileToSbc(path, &bytes, &error)) {
      PrintErrorWithContext(path, error);
      return 1;
    }
    load = Simple::Byte::LoadModuleFromBytes(bytes);
  } else if (HasExt(path, ".sir")) {
    std::string text;
    if (!ReadFileText(path, &text, &error) || !CompileSirToSbc(text, path, &bytes, &error)) {
      PrintError(error);
      return 1;
    }
    load = Simple::Byte::LoadModuleFromBytes(bytes);
  } else {
    load = Simple::Byte::LoadModuleFromFile(path);
  }
  if (!load.ok) {
    PrintError("load failed: " + load.error);
    return 1;
  }

  if (verify) {
    Simple::Byte::VerifyResult vr = Simple::Byte::VerifyModule(load.module);
    if (!vr.ok) {
      PrintError("verify failed: " + vr.error);
      return 1;
    }
  }

  Simple::VM::ExecOptions exec_options;
  exec_options.force_interpreter = force_interpreter;
  Simple::VM::ExecResult exec = Simple::VM::ExecuteModule(load.module, verify, use_jit, exec_options);
  if (print_jit_stats) {
    Simple::VM::Jit::LlvmJitStatus llvm_status = Simple::VM::Jit::GetLlvmJitStatus();
    std::cerr << "[jit] requested=" << (use_jit ? "yes" : "no")
              << " force_interpreter=" << (force_interpreter ? "yes" : "no")
              << " llvm=" << (llvm_status.available ? "on" : "off") << "\n";
    const size_t count = std::max({exec.jit_tiers.size(), exec.call_counts.size(), exec.opcode_counts.size(),
                                   exec.compile_counts.size(), exec.jit_dispatch_counts.size(),
                                   exec.jit_compiled_exec_counts.size(), exec.jit_tier1_exec_counts.size(),
                                   exec.llvm_reject_counts.size()});
    if (!exec.jit_status_counts.empty()) {
      std::cerr << "[jit] status";
      for (size_t i = 0; i < exec.jit_status_counts.size(); ++i) {
        const auto code = static_cast<Simple::VM::Jit::JitStatusCode>(i);
        std::cerr << " " << Simple::VM::Jit::JitStatusCodeName(code)
                  << "=" << exec.jit_status_counts[i];
      }
      std::cerr << "\n";
    }
    for (size_t i = 0; i < count; ++i) {
      uint32_t calls = i < exec.call_counts.size() ? exec.call_counts[i] : 0;
      uint64_t ops = i < exec.opcode_counts.size() ? exec.opcode_counts[i] : 0;
      uint32_t compiles = i < exec.compile_counts.size() ? exec.compile_counts[i] : 0;
      uint32_t dispatches = i < exec.jit_dispatch_counts.size() ? exec.jit_dispatch_counts[i] : 0;
      uint32_t compiled_execs = i < exec.jit_compiled_exec_counts.size() ? exec.jit_compiled_exec_counts[i] : 0;
      uint32_t tier1_execs = i < exec.jit_tier1_exec_counts.size() ? exec.jit_tier1_exec_counts[i] : 0;
      uint32_t llvm_rejects = i < exec.llvm_reject_counts.size() ? exec.llvm_reject_counts[i] : 0;
      std::string llvm_reject_reason = i < exec.llvm_reject_reasons.size() ? exec.llvm_reject_reasons[i] : "";
      Simple::VM::JitTier tier = i < exec.jit_tiers.size() ? exec.jit_tiers[i] : Simple::VM::JitTier::None;
      if (calls == 0 && ops == 0 && compiles == 0 && dispatches == 0 && compiled_execs == 0 && tier1_execs == 0 &&
          llvm_rejects == 0 && tier == Simple::VM::JitTier::None) {
        continue;
      }
      std::cerr << "[jit] func#" << i;
      const std::string function_name = JitFunctionName(load.module, i);
      if (!function_name.empty()) std::cerr << " name=\"" << function_name << "\"";
      std::cerr << " tier=" << JitTierName(tier)
                << " calls=" << calls
                << " opcodes=" << ops
                << " compiles=" << compiles
                << " dispatch=" << dispatches
                << " compiled_exec=" << compiled_execs
                << " tier1_exec=" << tier1_execs
                << " llvm_reject=" << llvm_rejects;
      if (!llvm_reject_reason.empty()) std::cerr << " reason=\"" << llvm_reject_reason << "\"";
      std::cerr << "\n";
    }
  }
  if (exec.status == Simple::VM::ExecStatus::Trapped) {
    PrintError("runtime trap: " + exec.error);
    return 1;
  }

  return exec.exit_code;
}
