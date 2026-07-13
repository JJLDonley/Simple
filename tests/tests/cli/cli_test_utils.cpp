#include "cli/cli_test_utils.h"
#include "test_utils.h"

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace Simple::VM::Tests {

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
  const bool search_path = !executable_path.has_parent_path();
  const std::string executable = search_path
                                     ? executable_path.string()
                                     : std::filesystem::absolute(executable_path).string();
#ifdef _WIN32
  auto quote_argument = [](const std::wstring& argument) {
    std::wstring quoted = L"\"";
    size_t backslashes = 0;
    for (wchar_t c : argument) {
      if (c == L'\\') {
        ++backslashes;
      } else if (c == L'\"') {
        quoted.append(backslashes * 2 + 1, L'\\');
        quoted += c;
        backslashes = 0;
      } else {
        quoted.append(backslashes, L'\\');
        backslashes = 0;
        quoted += c;
      }
    }
    quoted.append(backslashes * 2, L'\\');
    quoted += L'\"';
    return quoted;
  };

  const std::wstring executable_wide = std::filesystem::path(executable).wstring();
  std::wstring command_line = quote_argument(executable_wide);
  for (const auto& argument : arguments) {
    command_line += L" ";
    command_line += quote_argument(std::filesystem::path(argument).wstring());
  }
  std::vector<wchar_t> mutable_command(command_line.begin(), command_line.end());
  mutable_command.push_back(L'\0');

  SECURITY_ATTRIBUTES security{};
  security.nLength = sizeof(security);
  security.bInheritHandle = TRUE;
  std::vector<HANDLE> opened_handles;
  auto open_redirect = [&](const std::filesystem::path& path, DWORD access,
                           DWORD creation, DWORD standard_handle) -> HANDLE {
    if (path.empty()) return GetStdHandle(standard_handle);
    HANDLE handle = CreateFileW(path.wstring().c_str(), access,
                                FILE_SHARE_READ | FILE_SHARE_WRITE, &security,
                                creation, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (handle != INVALID_HANDLE_VALUE) opened_handles.push_back(handle);
    return handle;
  };

  STARTUPINFOW startup{};
  startup.cb = sizeof(startup);
  startup.dwFlags = STARTF_USESTDHANDLES;
  startup.hStdInput = open_redirect(stdin_path, GENERIC_READ, OPEN_EXISTING,
                                    STD_INPUT_HANDLE);
  startup.hStdOutput = open_redirect(stdout_path, GENERIC_WRITE, CREATE_ALWAYS,
                                     STD_OUTPUT_HANDLE);
  startup.hStdError = open_redirect(stderr_path, GENERIC_WRITE, CREATE_ALWAYS,
                                    STD_ERROR_HANDLE);
  const bool valid_handles = startup.hStdInput != INVALID_HANDLE_VALUE &&
                             startup.hStdOutput != INVALID_HANDLE_VALUE &&
                             startup.hStdError != INVALID_HANDLE_VALUE;
  PROCESS_INFORMATION process{};
  const BOOL created = valid_handles
                           ? CreateProcessW(search_path ? nullptr : executable_wide.c_str(),
                                            mutable_command.data(), nullptr, nullptr, TRUE, 0,
                                            nullptr, nullptr,
                                            &startup, &process)
                           : FALSE;
  int result = -1;
  if (created) {
    WaitForSingleObject(process.hProcess, INFINITE);
    DWORD exit_code = 0;
    if (GetExitCodeProcess(process.hProcess, &exit_code)) {
      result = static_cast<int>(exit_code);
    }
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
  }
  for (HANDLE handle : opened_handles) CloseHandle(handle);
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
    if (search_path) execvp(executable.c_str(), argv.data());
    else execv(executable.c_str(), argv.data());
    _exit(127);
  }
  int status = 0;
  if (waitpid(child, &status, 0) < 0) return -1;
  if (WIFEXITED(status)) return WEXITSTATUS(status);
  if (WIFSIGNALED(status)) return 128 + WTERMSIG(status);
  return status;
#endif
}

int RunCliTool(const std::string& tool, const std::vector<std::string>& arguments) {
  return RunProcess(CliToolPath(tool), arguments);
}

bool RunCliToolQuiet(const std::string& tool, const std::vector<std::string>& arguments) {
  const auto output = TestTempPath(tool + "_quiet_stdout.txt");
  const auto error = TestTempPath(tool + "_quiet_stderr.txt");
  return RunProcess(CliToolPath(tool), arguments, {}, output, error) == 0;
}

std::string RunCliToolCaptureStderr(const std::string& tool,
                                    const std::vector<std::string>& arguments,
                                    const std::string& temp_name,
                                    int* out_exit_code) {
  const auto output = TestTempPath(temp_name + ".stdout");
  const auto error = TestTempPath(temp_name);
  const int result = RunProcess(CliToolPath(tool), arguments, {}, output, error);
  if (out_exit_code) *out_exit_code = result;
  std::ifstream in(error);
  std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  in.close();
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

std::string RunProcessCaptureStdout(const std::filesystem::path& executable,
                                    const std::vector<std::string>& arguments,
                                    const std::string& temp_name,
                                    int* out_exit_code) {
  const auto path = TestTempPath(temp_name);
  const int result = RunProcess(executable, arguments, {}, path, {});
  if (out_exit_code) *out_exit_code = result;
  std::ifstream in(path);
  std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  in.close();
  std::filesystem::remove(path);
  return text;
}


} // namespace Simple::VM::Tests
