#include "build_contract.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <system_error>
#if defined(__linux__)
#include <unistd.h>
#endif

namespace Simple::CLI {
namespace {

std::string QuoteArg(const std::string& arg) {
  std::string out = "\"";
  for (char c : arg) {
    if (c == '"') out += "\\\"";
    else out += c;
  }
  out += "\"";
  return out;
}

std::string ExecutablePath(const char* argv0) {
  namespace fs = std::filesystem;
#if defined(__linux__)
  char buf[4096];
  const ssize_t n = ::readlink("/proc/self/exe", buf, sizeof(buf) - 1);
  if (n > 0) {
    buf[n] = '\0';
    return std::string(buf);
  }
#endif
  if (argv0 && *argv0) {
    std::error_code ec;
    fs::path p = fs::weakly_canonical(fs::path(argv0), ec);
    if (!ec) return p.string();
    return fs::absolute(fs::path(argv0)).string();
  }
  return {};
}

} // namespace

bool ResolveBuildLayoutPaths(const char* argv0, BuildLayoutPaths* out) {
  if (!out) return false;
  namespace fs = std::filesystem;
  auto try_source_layout = [&](const fs::path& root) -> bool {
    if (root.empty()) return false;
    const fs::path vm_inc = root / "VM" / "include";
    const fs::path byte_inc = root / "Byte" / "include";
    const fs::path lib_dir = root / "bin";
    if (fs::exists(vm_inc / "vm.h") && fs::exists(byte_inc / "sbc_loader.h") &&
        fs::exists(lib_dir / "libsimplevm_runtime.a")) {
      out->vm_include = vm_inc.string();
      out->byte_include = byte_inc.string();
      out->lib_dir = lib_dir.string();
      return true;
    }
    return false;
  };
  auto try_install_layout = [&](const fs::path& prefix) -> bool {
    if (prefix.empty()) return false;
    const fs::path include_dir = prefix / "include" / "simplevm";
    const fs::path lib_dir = prefix / "lib";
    if (fs::exists(include_dir / "vm.h") && fs::exists(include_dir / "sbc_loader.h") &&
        fs::exists(lib_dir / "libsimplevm_runtime.a")) {
      out->vm_include = include_dir.string();
      out->byte_include = include_dir.string();
      out->lib_dir = lib_dir.string();
      return true;
    }
    return false;
  };
#ifdef SIMPLEVM_PROJECT_ROOT
  fs::path configured_root = SIMPLEVM_PROJECT_ROOT;
  if (try_source_layout(configured_root)) return true;
#endif
  const std::string exe_text = ExecutablePath(argv0);
  if (exe_text.empty()) return false;
  fs::path exe_path = fs::path(exe_text);
  fs::path dir = exe_path.parent_path();
  if (try_source_layout(dir.parent_path())) return true;
  if (try_source_layout(dir)) return true;
  if (dir.filename() == "bin" && try_install_layout(dir.parent_path())) return true;
  if (try_install_layout(dir)) return true;
  return false;
}

bool WriteEmbeddedRunner(const std::string& path,
                         const std::vector<uint8_t>& bytes,
                         std::string* error) {
  std::ofstream out(path);
  if (!out) {
    if (error) *error = "failed to open runner output file";
    return false;
  }
  out << "#include <cstdint>\n"
         "#include <vector>\n"
         "#include <string>\n"
         "#include <iostream>\n"
         "#include \"sbc_loader.h\"\n"
         "#include \"sbc_verifier.h\"\n"
         "#include \"vm.h\"\n"
         "\n"
         "static const uint8_t kSbcData[] = {";
  for (size_t i = 0; i < bytes.size(); ++i) {
    if (i % 12 == 0) out << "\n  ";
    out << "0x" << std::hex << std::uppercase << static_cast<int>(bytes[i]) << std::dec;
    if (i + 1 < bytes.size()) out << ", ";
  }
  out << "\n};\n\n"
         "int main() {\n"
         "  std::vector<uint8_t> bytes(kSbcData, kSbcData + sizeof(kSbcData));\n"
         "  auto load = Simple::Byte::LoadModuleFromBytes(bytes);\n"
         "  if (!load.ok) {\n"
         "    std::cerr << \"load failed: \" << load.error << \"\\n\";\n"
         "    return 1;\n"
         "  }\n"
         "  auto vr = Simple::Byte::VerifyModule(load.module);\n"
         "  if (!vr.ok) {\n"
         "    std::cerr << \"verify failed: \" << vr.error << \"\\n\";\n"
         "    return 1;\n"
         "  }\n"
         "  auto exec = Simple::VM::ExecuteModule(load.module, true);\n"
         "  if (exec.status == Simple::VM::ExecStatus::Trapped) {\n"
         "    std::cerr << \"runtime trap: \" << exec.error << \"\\n\";\n"
         "    return 1;\n"
         "  }\n"
         "  return exec.exit_code;\n"
         "}\n";
  if (!out) {
    if (error) *error = "failed to write runner source";
    return false;
  }
  return true;
}

bool BuildEmbeddedExecutable(const BuildLayoutPaths& layout,
                             const std::vector<uint8_t>& bytes,
                             const std::string& out_path,
                             bool is_static,
                             std::string* error) {
  namespace fs = std::filesystem;
  fs::path tmp_dir = fs::temp_directory_path() / ("simple_embed_" + std::to_string(std::rand()));
  std::error_code ec;
  fs::create_directories(tmp_dir, ec);
  if (ec) {
    if (error) *error = "failed to create temp dir for build";
    return false;
  }
  fs::path runner_path = tmp_dir / "embedded_main.cpp";
  if (!WriteEmbeddedRunner(runner_path.string(), bytes, error)) return false;

  fs::path vm_include(layout.vm_include);
  fs::path byte_include(layout.byte_include);
  fs::path lib_dir(layout.lib_dir);
  fs::path runtime_lib = is_static ? (lib_dir / "libsimplevm_runtime.a")
                                   : (lib_dir / "libsimplevm_runtime.so");
  if (!fs::exists(runtime_lib)) {
    if (error) {
      *error = std::string("missing runtime library: ") + runtime_lib.string() +
               " (rebuild with ./Simple/build.sh or reinstall simple runtime)";
    }
    return false;
  }

  std::string cmd = "g++ -std=c++17 -O2 -Wall -Wextra ";
  cmd += "-I" + QuoteArg(vm_include.string()) + " ";
  cmd += "-I" + QuoteArg(byte_include.string()) + " ";
  cmd += QuoteArg(runner_path.string()) + " ";
  cmd += QuoteArg(runtime_lib.string()) + " ";
  if (!is_static) cmd += "-Wl,-rpath," + QuoteArg(lib_dir.string()) + " ";
  cmd += "-ldl ";
  cmd += "-lffi ";
  cmd += "-o " + QuoteArg(out_path);

  int rc = std::system(cmd.c_str());
  if (rc != 0) {
    if (error) *error = "failed to compile embedded executable";
    return false;
  }
  return true;
}

} // namespace Simple::CLI
