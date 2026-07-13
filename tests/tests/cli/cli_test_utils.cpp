#include "cli/cli_test_utils.h"

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#ifndef _WIN32
#include <sys/wait.h>
#endif

namespace Simple::VM::Tests {
namespace {

const char* NullDevice() {
#ifdef _WIN32
  return "NUL";
#else
  return "/dev/null";
#endif
}

std::string WrapSystemCommand(const std::string& command) {
#ifdef _WIN32
  // cmd.exe requires an outer quote when the executable path is quoted.
  return "\"" + command + "\"";
#else
  return command;
#endif
}

struct TempDirectory {
  std::filesystem::path path;

  TempDirectory() {
    const auto seed = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto base = std::filesystem::temp_directory_path();
    for (uint32_t attempt = 0; attempt < 100; ++attempt) {
      path = base / ("simplevm_tests_" + std::to_string(seed) + "_" +
                     std::to_string(attempt));
      std::error_code error;
      if (std::filesystem::create_directory(path, error)) return;
    }
    path = base;
  }

  ~TempDirectory() {
    std::error_code error;
    if (path != std::filesystem::temp_directory_path()) {
      std::filesystem::remove_all(path, error);
    }
  }
};

const std::filesystem::path& TestTempDirectory() {
  static const TempDirectory directory;
  return directory.path;
}

} // namespace

std::filesystem::path CliTempPath(const std::string& name) {
  return TestTempDirectory() / name;
}

std::filesystem::path CliTempExecutablePath(const std::string& name) {
#ifdef _WIN32
  return CliTempPath(name + ".exe");
#else
  return CliTempPath(name);
#endif
}

std::string CliToolPath(const std::string& name) {
  std::string executable = name;
#ifdef _WIN32
  executable += ".exe";
#endif
  const auto build_path = std::filesystem::path("build") / "bin" / executable;
  if (std::filesystem::exists(build_path)) return build_path.string();
  return (std::filesystem::path("bin") / executable).string();
}

std::string CliSvmCommand(const std::string& arguments) {
  return "\"" + CliToolPath("svm") + "\"" +
         (arguments.empty() ? std::string{} : " " + arguments);
}

int CliExitCodeFromSystemResult(int result) {
#ifdef _WIN32
  return result;
#else
  if (result == -1) return -1;
  if (WIFEXITED(result)) return WEXITSTATUS(result);
  if (WIFSIGNALED(result)) return 128 + WTERMSIG(result);
  return result;
#endif
}

bool RunCliCommandQuiet(const std::string& command) {
  const std::string null_device = NullDevice();
  return std::system(WrapSystemCommand(command + " >" + null_device +
                                       " 2>" + null_device).c_str()) == 0;
}

bool RunCliCommandRaw(const std::string& command) {
  return std::system(WrapSystemCommand(command).c_str()) == 0;
}

std::string RunCliCaptureStdout(const std::string& command,
                                const std::string& temp_name,
                                int* out_exit_code) {
  const auto path = CliTempPath(temp_name);
  const int result = std::system(
      WrapSystemCommand(command + " > \"" + path.string() + "\"").c_str());
  if (out_exit_code) *out_exit_code = CliExitCodeFromSystemResult(result);
  std::ifstream in(path);
  std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  std::filesystem::remove(path);
  return text;
}

std::string RunCliCaptureStderr(const std::string& command,
                                const std::string& temp_name,
                                int* out_exit_code) {
  const auto path = CliTempPath(temp_name);
  const int result = std::system(
      WrapSystemCommand(command + " 1>" + NullDevice() + " 2> \"" +
                        path.string() + "\"").c_str());
  if (out_exit_code) *out_exit_code = CliExitCodeFromSystemResult(result);
  std::ifstream in(path);
  std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  std::filesystem::remove(path);
  return text;
}

} // namespace Simple::VM::Tests
