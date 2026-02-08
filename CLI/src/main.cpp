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
#include "lang_parser.h"
#include "lang_reserved.h"
#include "lang_validate.h"
#include "lang_sir.h"
#include "lsp_server.h"
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

bool LooksLikeProjectRoot(const std::filesystem::path& root) {
  namespace fs = std::filesystem;
  return fs::exists(root / "VM" / "include" / "vm.h") &&
         fs::exists(root / "Lang" / "include" / "lang_parser.h") &&
         fs::exists(root / "Byte" / "include" / "sbc_loader.h");
}

std::filesystem::path ResolveImportProjectRoot(const std::filesystem::path& entry_path) {
  namespace fs = std::filesystem;
  std::error_code ec;
#ifdef SIMPLEVM_PROJECT_ROOT
  fs::path configured_root = fs::weakly_canonical(fs::path(SIMPLEVM_PROJECT_ROOT), ec);
  if (!ec && !configured_root.empty() && LooksLikeProjectRoot(configured_root)) {
    return configured_root;
  }
#endif
  fs::path cursor = fs::weakly_canonical(entry_path, ec);
  if (ec || cursor.empty()) cursor = fs::absolute(entry_path);
  if (fs::is_regular_file(cursor)) cursor = cursor.parent_path();
  while (!cursor.empty()) {
    if (LooksLikeProjectRoot(cursor)) return cursor;
    if (!cursor.has_parent_path() || cursor.parent_path() == cursor) break;
    cursor = cursor.parent_path();
  }
  fs::path cwd = fs::weakly_canonical(fs::current_path(), ec);
  if (!ec && !cwd.empty()) return cwd;
  return fs::current_path();
}

bool BuildSimpleFileIndex(const std::filesystem::path& project_root,
                          std::unordered_map<std::string, std::vector<std::filesystem::path>>* out) {
  if (!out) return false;
  out->clear();
  namespace fs = std::filesystem;
  std::error_code ec;
  fs::recursive_directory_iterator it(
      project_root,
      fs::directory_options::skip_permission_denied,
      ec);
  if (ec) return false;
  for (const auto& entry : it) {
    if (!entry.is_regular_file()) continue;
    const fs::path& path = entry.path();
    if (path.extension() != ".simple") continue;
    (*out)[path.filename().string()].push_back(fs::weakly_canonical(path, ec));
    if (ec) {
      ec.clear();
      (*out)[path.filename().string()].push_back(fs::absolute(path));
    }
  }
  return true;
}

bool ResolveProjectRootImportPath(
    const std::unordered_map<std::string, std::vector<std::filesystem::path>>& index,
    const std::string& import_path,
    std::filesystem::path* out,
    std::string* error) {
  if (!out) return false;
  const std::string target = import_path.size() >= 7 &&
                                     import_path.rfind(".simple") == import_path.size() - 7
                                 ? import_path
                                 : import_path + ".simple";
  auto it = index.find(target);
  if (it == index.end() || it->second.empty()) {
    if (error) *error = "import not found in project root: " + import_path;
    return false;
  }
  if (it->second.size() > 1) {
    std::vector<std::string> matches;
    matches.reserve(it->second.size());
    for (const auto& p : it->second) matches.push_back(p.string());
    std::sort(matches.begin(), matches.end());
    std::string details;
    const size_t limit = std::min<size_t>(5, matches.size());
    for (size_t i = 0; i < limit; ++i) {
      if (i) details += ", ";
      details += matches[i];
    }
    if (matches.size() > limit) details += ", ...";
    if (error) *error = "ambiguous import path '" + import_path + "' matched: " + details;
    return false;
  }
  *out = it->second.front();
  return true;
}

bool ResolveLocalImportPath(const std::filesystem::path& base_dir,
                            const std::unordered_map<std::string, std::vector<std::filesystem::path>>& project_index,
                            const std::string& import_path,
                            std::filesystem::path* out,
                            std::string* error) {
  if (!out) return false;
  namespace fs = std::filesystem;
  fs::path raw(import_path);
  const bool has_separator =
      import_path.find('/') != std::string::npos || import_path.find('\\') != std::string::npos;
  const bool explicit_relative = raw.is_relative() && !import_path.empty() &&
                                 (import_path[0] == '.' || has_separator);

  if (raw.is_absolute()) {
    if (fs::exists(raw)) {
      *out = fs::weakly_canonical(raw);
      return true;
    }
    if (!raw.has_extension()) {
      fs::path with_ext = raw;
      with_ext += ".simple";
      if (fs::exists(with_ext)) {
        *out = fs::weakly_canonical(with_ext);
        return true;
      }
    }
  } else if (explicit_relative) {
    fs::path cand = base_dir / raw;
    if (fs::exists(cand)) {
      *out = fs::weakly_canonical(cand);
      return true;
    }
    if (!raw.has_extension()) {
      fs::path with_ext = base_dir / (import_path + ".simple");
      if (fs::exists(with_ext)) {
        *out = fs::weakly_canonical(with_ext);
        return true;
      }
    }
  } else {
    if (ResolveProjectRootImportPath(project_index, import_path, out, error)) {
      return true;
    }
  }
  if (error && error->empty()) *error = "unsupported import path: " + import_path;
  return false;
}

bool AppendProgramWithLocalImports(const std::filesystem::path& file_path,
                                   const std::unordered_map<std::string, std::vector<std::filesystem::path>>& project_index,
                                   Simple::Lang::Program* out,
                                   std::unordered_set<std::string>* visiting,
                                   std::unordered_set<std::string>* visited,
                                   std::string* error) {
  if (!out || !visiting || !visited) return false;
  namespace fs = std::filesystem;
  std::error_code ec;
  fs::path canon = fs::weakly_canonical(file_path, ec);
  if (ec || canon.empty()) canon = fs::absolute(file_path);
  const std::string key = canon.string();
  if (visited->find(key) != visited->end()) return true;
  if (!visiting->insert(key).second) {
    if (error) *error = "cyclic import detected: " + key;
    return false;
  }

  std::string text;
  if (!ReadFileText(key, &text, error)) {
    visiting->erase(key);
    return false;
  }
  Simple::Lang::Program program;
  std::string parse_error;
  if (!Simple::Lang::ParseProgramFromString(text, &program, &parse_error)) {
    if (error) *error = key + ": " + parse_error;
    visiting->erase(key);
    return false;
  }

  const fs::path base_dir = canon.parent_path();
  for (const auto& decl : program.decls) {
    if (decl.kind != Simple::Lang::DeclKind::Import) continue;
    if (Simple::Lang::IsReservedImportPath(decl.import_decl.path)) continue;
    fs::path import_file;
    if (!ResolveLocalImportPath(base_dir, project_index, decl.import_decl.path, &import_file, error)) {
      visiting->erase(key);
      return false;
    }
    if (!AppendProgramWithLocalImports(import_file, project_index, out, visiting, visited, error)) {
      visiting->erase(key);
      return false;
    }
  }

  for (auto& decl : program.decls) {
    if (decl.kind == Simple::Lang::DeclKind::Import &&
        !Simple::Lang::IsReservedImportPath(decl.import_decl.path)) {
      continue;
    }
    out->decls.push_back(std::move(decl));
  }
  for (auto& stmt : program.top_level_stmts) {
    out->top_level_stmts.push_back(std::move(stmt));
  }

  visiting->erase(key);
  visited->insert(key);
  return true;
}

bool LoadSimpleProgramWithImports(const std::string& entry_path,
                                  Simple::Lang::Program* out,
                                  std::string* error) {
  if (!out) return false;
  out->decls.clear();
  const std::filesystem::path project_root = ResolveImportProjectRoot(entry_path);
  std::unordered_map<std::string, std::vector<std::filesystem::path>> project_index;
  if (!BuildSimpleFileIndex(project_root, &project_index)) {
    if (error) *error = "failed to enumerate .simple files under project root: " + project_root.string();
    return false;
  }
  std::unordered_set<std::string> visiting;
  std::unordered_set<std::string> visited;
  return AppendProgramWithLocalImports(entry_path, project_index, out, &visiting, &visited, error);
}

bool ValidateSimpleFile(const std::string& path, std::string* error) {
  Simple::Lang::Program program;
  if (!LoadSimpleProgramWithImports(path, &program, error)) return false;
  return Simple::Lang::ValidateProgram(program, error);
}

bool EmitSirFromSimpleFile(const std::string& path, std::string* out, std::string* error) {
  Simple::Lang::Program program;
  if (!LoadSimpleProgramWithImports(path, &program, error)) return false;
  return Simple::Lang::EmitSir(program, out, error);
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

bool CompileSimpleFileToSbc(const std::string& path,
                            std::vector<uint8_t>* out,
                            std::string* error) {
  std::string sir;
  if (!EmitSirFromSimpleFile(path, &sir, error)) {
    if (error) *error = "simple compile failed (" + path + "): " + *error;
    return false;
  }
  return CompileSirToSbc(sir, path, out, error);
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

struct BuildLayoutPaths {
  std::string vm_include;
  std::string byte_include;
  std::string lib_dir;
};

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
  if (try_source_layout(configured_root)) {
    return true;
  }
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

std::string BaseName(const char* argv0) {
  if (!argv0 || !*argv0) return "simplevm";
  std::string name = argv0;
  const size_t slash = name.find_last_of("/\\");
  if (slash != std::string::npos) name = name.substr(slash + 1);
  if (name.empty()) return "simplevm";
  return name;
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
  if (!is_static) {
    cmd += "-Wl,-rpath," + QuoteArg(lib_dir.string()) + " ";
  }
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
      const bool maybe_path = before.find('/') != std::string::npos ||
                              before.find('\\') != std::string::npos ||
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
  std::cerr << "error[E0001]: " << TrimCopy(message) << "\n";
}

std::string DiagnosticHelpFor(const std::string& message) {
  if (message.find("unexpected character") != std::string::npos) {
    return "remove unsupported characters or escape them if inside literals";
  }
  if (message.find("unsupported import path") != std::string::npos) {
    return "use a reserved stdlib import, a relative/absolute path, or a unique bare filename under project root";
  }
  if (message.find("import not found in project root") != std::string::npos) {
    return "add the target .simple file under project root or use an explicit relative path";
  }
  if (message.find("ambiguous import path") != std::string::npos) {
    return "rename duplicate files or use an explicit relative path to disambiguate";
  }
  if (message.find("undeclared identifier") != std::string::npos) {
    return "declare the symbol in scope, or fix a typo in the identifier name";
  }
  if (message.find("unterminated block") != std::string::npos) {
    return "add the missing closing '}' for this block";
  }
  if (message.find("expected") != std::string::npos) {
    return "check surrounding syntax near the highlighted token";
  }
  return {};
}

void PrintDiagnosticHelp(const std::string& message) {
  const std::string hint = DiagnosticHelpFor(message);
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
  std::cerr << "error[E0001]: " << loc.message << "\n";
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
  const bool simple_only = (tool_name == "simple");
  if (argc < 2) {
    std::cerr << "usage:\n";
    if (simple_only) {
      std::cerr << "  " << tool_name << " run <file.simple> [--no-verify]\n"
                << "  " << tool_name
                << " build <file.simple> [--out <file.exe|file.sbc>] [-d|--dynamic|-s|--static] [--no-verify]\n"
                << "  " << tool_name
                << " compile <file.simple> [--out <file.exe|file.sbc>] [-d|--dynamic|-s|--static] [--no-verify]\n"
                << "  " << tool_name << " emit -ir <file.simple> [--out <file.sir>]\n"
                << "  " << tool_name << " emit -sbc <file.simple> [--out <file.sbc>] [--no-verify]\n"
                << "  " << tool_name << " check <file.simple>\n"
                << "  " << tool_name << " lsp\n"
                << "  " << tool_name << " <file.simple> [--no-verify]\n";
    } else {
      std::cerr << "  " << tool_name << " run <module.sbc|file.sir|file.simple> [--no-verify]\n"
                << "  " << tool_name << " build <file.sir|file.simple> [--out <file.sbc>] [--no-verify]\n"
                << "  " << tool_name << " compile <file.sir|file.simple> [--out <file.sbc>] [--no-verify]\n"
                << "  " << tool_name << " emit -ir <file.simple> [--out <file.sir>]\n"
                << "  " << tool_name << " emit -sbc <file.sir|file.simple> [--out <file.sbc>] [--no-verify]\n"
                << "  " << tool_name << " check <file.sbc|file.sir|file.simple>\n"
                << "  " << tool_name << " lsp\n"
                << "  " << tool_name << " <module.sbc|file.sir|file.simple> [--no-verify]\n";
    }
    return 1;
  }

  const std::string cmd = argv[1];
  const bool build_cmd = (cmd == "build" || cmd == "compile");
  const bool is_command = (cmd == "run" || build_cmd || cmd == "check" || cmd == "emit" || cmd == "lsp");
  const std::string path = is_command ? (argc > 2 ? argv[2] : "") : cmd;
  bool verify = true;
  bool build_exe = false;
  bool build_static = false;
  bool build_mode_explicit = false;
  for (int i = 2; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "--no-verify") {
      verify = false;
    } else if (arg == "-d" || arg == "--dynamic") {
      build_exe = true;
      build_static = false;
      build_mode_explicit = true;
    } else if (arg == "-s" || arg == "--static") {
      build_exe = true;
      build_static = true;
      build_mode_explicit = true;
    }
  }

  if (is_command && cmd != "lsp" && path.empty()) {
    PrintError("missing input file");
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
    const std::string emit_path = argv[3];
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
    if (!build_mode_explicit && simple_only && (out_path.empty() || !HasExt(out_path, ".sbc"))) {
      build_exe = true;
    }
    if (out_path.empty()) {
      out_path = build_exe ? ReplaceExt(input_path, "") : ReplaceExt(input_path, ".sbc");
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
      BuildLayoutPaths layout;
      if (!ResolveBuildLayoutPaths(argv[0], &layout)) {
        PrintError("unable to resolve runtime/include paths; install simple runtime or run from source tree");
        return 1;
      }
      if (!BuildEmbeddedExecutable(layout, bytes, out_path, build_static, &error)) {
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
    if (simple_only && !HasExt(path, ".simple")) {
      PrintError("simple expects .simple input");
      return 1;
    }
  }

  Simple::Byte::LoadResult load{};
  std::vector<uint8_t> bytes;
  std::string error;
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
    if (simple_only) {
      PrintError("simple expects .simple input");
      return 1;
    }
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

  Simple::VM::ExecResult exec = Simple::VM::ExecuteModule(load.module, verify);
  if (exec.status == Simple::VM::ExecStatus::Trapped) {
    PrintError("runtime trap: " + exec.error);
    return 1;
  }

  return exec.exit_code;
}
