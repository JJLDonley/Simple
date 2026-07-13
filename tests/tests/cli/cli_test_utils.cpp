#include "cli/cli_test_utils.h"

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#include <process.h>
#include <sys/stat.h>
#else
#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace Simple::VM::Tests {
namespace {

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

int RunProcess(const std::filesystem::path& executable_path,
               const std::vector<std::string>& arguments,
               const std::filesystem::path& stdin_path,
               const std::filesystem::path& stdout_path,
               const std::filesystem::path& stderr_path) {
  const std::string executable = std::filesystem::absolute(executable_path).string();
#ifdef _WIN32
  struct Redirect {
    int target = -1;
    int saved = -1;
    int opened = -1;
  };
  std::vector<Redirect> redirects;
  auto redirect = [&](int target, const std::filesystem::path& path, int flags) -> bool {
    if (path.empty()) return true;
    Redirect value;
    value.target = target;
    value.saved = _dup(target);
    value.opened = _open(path.string().c_str(), flags | _O_BINARY, _S_IREAD | _S_IWRITE);
    if (value.saved < 0 || value.opened < 0 || _dup2(value.opened, target) != 0) {
      if (value.opened >= 0) _close(value.opened);
      if (value.saved >= 0) _close(value.saved);
      return false;
    }
    redirects.push_back(value);
    return true;
  };
  const bool redirected = redirect(0, stdin_path, _O_RDONLY) &&
                          redirect(1, stdout_path, _O_WRONLY | _O_CREAT | _O_TRUNC) &&
                          redirect(2, stderr_path, _O_WRONLY | _O_CREAT | _O_TRUNC);
  std::vector<const char*> argv;
  argv.reserve(arguments.size() + 2);
  argv.push_back(executable.c_str());
  for (const auto& argument : arguments) argv.push_back(argument.c_str());
  argv.push_back(nullptr);
  const int result = redirected
                         ? static_cast<int>(_spawnv(_P_WAIT, executable.c_str(), argv.data()))
                         : -1;
  std::fflush(nullptr);
  for (auto it = redirects.rbegin(); it != redirects.rend(); ++it) {
    _dup2(it->saved, it->target);
    _close(it->opened);
    _close(it->saved);
  }
  return result;
#else
  const pid_t child = fork();
  if (child < 0) return -1;
  if (child == 0) {
    auto redirect = [](int target, const std::filesystem::path& path, int flags) {
      if (path.empty()) return true;
      const int opened = open(path.c_str(), flags, 0600);
      if (opened < 0 || dup2(opened, target) < 0) return false;
      close(opened);
      return true;
    };
    if (!redirect(0, stdin_path, O_RDONLY) ||
        !redirect(1, stdout_path, O_WRONLY | O_CREAT | O_TRUNC) ||
        !redirect(2, stderr_path, O_WRONLY | O_CREAT | O_TRUNC)) {
      _exit(126);
    }
    std::vector<char*> argv;
    argv.reserve(arguments.size() + 2);
    argv.push_back(const_cast<char*>(executable.c_str()));
    for (const auto& argument : arguments) argv.push_back(const_cast<char*>(argument.c_str()));
    argv.push_back(nullptr);
    execv(executable.c_str(), argv.data());
    _exit(127);
  }
  int status = 0;
  if (waitpid(child, &status, 0) < 0) return -1;
  return CliExitCodeFromSystemResult(status);
#endif
}

int RunCliTool(const std::string& tool, const std::vector<std::string>& arguments) {
  return RunProcess(CliToolPath(tool), arguments);
}

bool RunCliToolQuiet(const std::string& tool, const std::vector<std::string>& arguments) {
  const auto output = CliTempPath(tool + "_quiet_stdout.txt");
  const auto error = CliTempPath(tool + "_quiet_stderr.txt");
  return RunProcess(CliToolPath(tool), arguments, {}, output, error) == 0;
}

std::string RunCliToolCaptureStderr(const std::string& tool,
                                    const std::vector<std::string>& arguments,
                                    const std::string& temp_name,
                                    int* out_exit_code) {
  const auto output = CliTempPath(temp_name + ".stdout");
  const auto error = CliTempPath(temp_name);
  const int result = RunProcess(CliToolPath(tool), arguments, {}, output, error);
  if (out_exit_code) *out_exit_code = result;
  std::ifstream in(error);
  std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  std::filesystem::remove(output);
  std::filesystem::remove(error);
  return text;
}

int RunCliSvm(const std::vector<std::string>& arguments) {
  return RunCliTool("svm", arguments);
}

bool RunCliSvmQuiet(const std::vector<std::string>& arguments) {
  return RunCliToolQuiet("svm", arguments);
}

std::string RunCliSvmCaptureStderr(const std::vector<std::string>& arguments,
                                   const std::string& temp_name,
                                   int* out_exit_code) {
  return RunCliToolCaptureStderr("svm", arguments, temp_name, out_exit_code);
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


} // namespace Simple::VM::Tests
