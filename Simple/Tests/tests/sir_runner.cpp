#include "sir_runner.h"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <vector>

#include "ir_compiler.h"
#include "ir_lang.h"
#include "sbc_loader.h"
#include "sbc_verifier.h"
#include "vm.h"

namespace Simple::VM::Tests {
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

int RunSbcBytes(const std::vector<uint8_t>& bytes, bool verify, std::string* error) {
  Simple::Byte::LoadResult load = Simple::Byte::LoadModuleFromBytes(bytes);
  if (!load.ok) {
    if (error) *error = "load failed: " + load.error;
    return 1;
  }
  if (verify) {
    Simple::Byte::VerifyResult vr = Simple::Byte::VerifyModule(load.module);
    if (!vr.ok) {
      if (error) *error = "verify failed: " + vr.error;
      return 1;
    }
  }
  Simple::VM::ExecResult exec = Simple::VM::ExecuteModule(load.module, verify);
  if (exec.status == Simple::VM::ExecStatus::Trapped) {
    if (error) *error = "runtime trap: " + exec.error;
    return 1;
  }
  return exec.exit_code;
}

} // namespace

int RunSirFile(const std::string& path, bool verify) {
  std::string text;
  std::string error;
  if (!ReadFileText(path, &text, &error)) {
    std::cerr << "sir load failed: " << path << " (" << error << ")\n";
    return 2;
  }
  std::vector<uint8_t> bytes;
  if (!CompileSirToSbc(text, path, &bytes, &error)) {
    std::cerr << error << "\n";
    return 2;
  }
  int exit_code = RunSbcBytes(bytes, verify, &error);
  if (!error.empty() && exit_code != 0) {
    std::cerr << error << "\n";
  }
  return exit_code;
}

int RunSirPerfDir(const std::string& dir, size_t iterations, bool verify) {
  namespace fs = std::filesystem;
  std::vector<fs::path> files;
  std::error_code ec;
  for (const auto& entry : fs::directory_iterator(dir, ec)) {
    if (ec) break;
    if (!entry.is_regular_file()) continue;
    if (entry.path().extension() != ".sir") continue;
    files.push_back(entry.path());
  }
  if (files.empty()) {
    std::cerr << "no .sir files in " << dir << "\n";
    return 2;
  }
  std::sort(files.begin(), files.end());

  for (const auto& path : files) {
    auto start = std::chrono::steady_clock::now();
    int last_exit = 0;
    for (size_t i = 0; i < iterations; ++i) {
      std::string text;
      std::string error;
      if (!ReadFileText(path.string(), &text, &error)) {
        std::cerr << "sir load failed: " << path.string() << " (" << error << ")\n";
        return 2;
      }
      std::vector<uint8_t> bytes;
      if (!CompileSirToSbc(text, path.string(), &bytes, &error)) {
        std::cerr << error << "\n";
        return 2;
      }
      last_exit = RunSbcBytes(bytes, verify, &error);
      if (!error.empty() && last_exit != 0) {
        std::cerr << error << "\n";
        return 2;
      }
    }
    auto end = std::chrono::steady_clock::now();
    auto total_us = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
    double per_iter = iterations ? static_cast<double>(total_us) / static_cast<double>(iterations)
                                 : 0.0;
    std::cout << path.filename().string() << ": " << iterations << " iters, " << total_us
              << " us total, " << per_iter << " us/iter, exit " << last_exit << "\n";
  }
  return 0;
}

} // namespace Simple::VM::Tests
