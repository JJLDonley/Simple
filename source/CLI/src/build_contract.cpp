#include "build_contract.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <system_error>

#include "platform/platform.h"

namespace Simple::CLI {

bool ResolveBuildLayoutPaths(const char* argv0, BuildLayoutPaths* out) {
  if (!out) return false;
  namespace fs = std::filesystem;
#ifdef SIMPLEVM_RUNTIME_STATIC_NAME
  const fs::path static_name = SIMPLEVM_RUNTIME_STATIC_NAME;
#else
  const fs::path static_name = "libsimplevm_runtime.a";
#endif
  auto try_source_layout = [&](const fs::path& root) -> bool {
    if (root.empty()) return false;
    const fs::path vm_inc = root / "source" / "VM" / "include";
    const fs::path byte_inc = root / "source" / "Byte" / "include";
    const fs::path root_bin = root / "bin";
    const fs::path build_bin = root / "build" / "bin";
    fs::path lib_dir;
    if (fs::exists(build_bin / static_name)) {
      lib_dir = build_bin;
    } else if (fs::exists(root_bin / static_name)) {
      lib_dir = root_bin;
    }
    if (fs::exists(vm_inc / "vm.h") && fs::exists(byte_inc / "sbc_loader.h") &&
        !lib_dir.empty()) {
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
        fs::exists(lib_dir / static_name)) {
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
  const std::string exe_text = Simple::Platform::ExecutablePath(argv0);
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
  fs::path tmp_dir = Simple::Platform::TempDirectory() /
                     ("simple_embed_" + std::to_string(std::rand()));
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
  fs::path runtime_lib;
  if (is_static) {
#ifdef SIMPLEVM_RUNTIME_STATIC_NAME
    runtime_lib = lib_dir / SIMPLEVM_RUNTIME_STATIC_NAME;
#else
    runtime_lib = lib_dir / "libsimplevm_runtime.a";
#endif
  } else {
#ifdef SIMPLEVM_RUNTIME_SHARED_NAME
    runtime_lib = lib_dir / SIMPLEVM_RUNTIME_SHARED_NAME;
#else
    runtime_lib = lib_dir /
                  (std::string("libsimplevm_runtime") +
                   Simple::Platform::SharedLibraryExtension());
#endif
  }
  if (!fs::exists(runtime_lib)) {
    if (error) {
      *error = std::string("missing runtime library: ") + runtime_lib.string() +
               " (rebuild with scripts/build/local.sh or reinstall simple runtime)";
    }
    return false;
  }

  Simple::Platform::NativeBuildRequest request;
  request.source = runner_path;
  request.output = out_path;
  request.include_dirs = {vm_include, byte_include};
  request.libraries = {runtime_lib};
  request.runtime_library_dir = lib_dir;
  request.dynamic_runtime = !is_static;
#ifdef SIMPLEVM_EMBED_LLVM_LINK_FLAGS
  if (is_static) request.extra_link_flags = SIMPLEVM_EMBED_LLVM_LINK_FLAGS;
#endif
  return Simple::Platform::BuildNativeExecutable(request, error);
}

} // namespace Simple::CLI
