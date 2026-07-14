#include "platform/platform.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <exception>
#include <limits>
#include <memory>
#include <mutex>
#include <process.h>
#include <thread>
#include <utility>
#include <vector>

namespace Simple::Platform {
namespace {
void SetError(std::string* error, const std::string& value) { if (error) *error = value; }
std::string WindowsError(const char* fallback) {
  const DWORD code = GetLastError();
  char* text = nullptr;
  const DWORD size = FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
                                        FORMAT_MESSAGE_IGNORE_INSERTS,
                                    nullptr, code, 0, reinterpret_cast<char*>(&text), 0, nullptr);
  std::string result = size && text ? std::string(text, size) : fallback;
  if (text) LocalFree(text);
  return result;
}
bool Utf8ToWide(const std::string& text, std::wstring* out) {
  if (!out) return false;
  out->clear();
  if (text.empty()) return true;
  if (text.size() > static_cast<size_t>((std::numeric_limits<int>::max)())) {
    SetLastError(ERROR_INVALID_PARAMETER);
    return false;
  }
  const int input_size = static_cast<int>(text.size());
  const int required =
      MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), input_size, nullptr, 0);
  if (required <= 0) return false;
  out->resize(static_cast<size_t>(required));
  return MultiByteToWideChar(
             CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), input_size, out->data(), required) ==
         required;
}

std::wstring QuoteWindowsArgument(const std::wstring& argument) {
  std::wstring quoted = L"\"";
  size_t backslashes = 0;
  for (wchar_t c : argument) {
    if (c == L'\\') {
      ++backslashes;
    } else if (c == L'\"') {
      quoted.append(backslashes * 2 + 1, L'\\');
      quoted.push_back(c);
      backslashes = 0;
    } else {
      quoted.append(backslashes, L'\\');
      backslashes = 0;
      quoted.push_back(c);
    }
  }
  quoted.append(backslashes * 2, L'\\');
  quoted.push_back(L'\"');
  return quoted;
}

void CloseNativeHandle(HANDLE* handle) {
  if (!handle || !*handle || *handle == INVALID_HANDLE_VALUE) return;
  CloseHandle(*handle);
  *handle = nullptr;
}

std::vector<std::string> SplitArguments(const std::string& text) {
  std::vector<std::string> arguments;
  std::string current;
  bool quoted = false;
  for (size_t i = 0; i < text.size(); ++i) {
    const char c = text[i];
    if (c == '"') {
      quoted = !quoted;
    } else if (std::isspace(static_cast<unsigned char>(c)) && !quoted) {
      if (!current.empty()) {
        arguments.push_back(std::move(current));
        current.clear();
      }
    } else {
      current += c;
    }
  }
  if (!current.empty()) arguments.push_back(std::move(current));
  return arguments;
}
}

OperatingSystem HostOperatingSystem() { return OperatingSystem::Windows; }
const char* OperatingSystemName() { return "windows"; }
const char* SharedLibraryExtension() { return ".dll"; }
const char* StaticLibraryExtension() { return ".lib"; }
char PathListDelimiter() { return ';'; }
int32_t CurrentProcessId() { return static_cast<int32_t>(GetCurrentProcessId()); }
int64_t MemoryPageSize() {
  SYSTEM_INFO info{};
  GetSystemInfo(&info);
  return static_cast<int64_t>(info.dwPageSize);
}
bool UtcTime(std::time_t value, std::tm* out) {
  return out && gmtime_s(out, &value) == 0;
}
std::FILE* OpenFile(const std::string& path, const char* mode) {
  std::FILE* file = nullptr;
  return fopen_s(&file, path.c_str(), mode) == 0 ? file : nullptr;
}

std::string ExecutablePath(const char* argv0) {
  std::vector<wchar_t> buffer(32768);
  const DWORD size = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
  if (size) return std::filesystem::path(std::wstring(buffer.data(), size)).string();
  if (!argv0 || !*argv0) return {};
  return std::filesystem::absolute(argv0).string();
}

bool GetEnvironment(const std::string& name, std::string* value) {
  if (!value) return false;
  char* raw = nullptr;
  size_t size = 0;
  if (_dupenv_s(&raw, &size, name.c_str()) != 0 || !raw) return false;
  value->assign(raw, size ? size - 1 : 0);
  std::free(raw);
  return true;
}
bool SetEnvironment(const std::string& name, const std::string& value) { return _putenv_s(name.c_str(), value.c_str()) == 0; }
bool UnsetEnvironment(const std::string& name) { return _putenv_s(name.c_str(), "") == 0; }

struct Process::Impl {
  HANDLE process = nullptr;
  HANDLE stdin_write = nullptr;
  HANDLE stdout_read = nullptr;
  HANDLE stderr_read = nullptr;
  mutable std::mutex state_mutex;
  std::mutex wait_mutex;
  std::mutex stdin_mutex;
  mutable std::mutex output_mutex;
  bool exited = false;
  bool closed = false;
  int32_t exit_code = -1;
  std::string stdout_text;
  std::string stderr_text;
  std::thread stdout_reader;
  std::thread stderr_reader;
};

Process::Process(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
Process::~Process() {
  std::string ignored;
  (void)Close(&ignored);
}
void ReadProcessPipe(HANDLE handle, std::string* output, std::mutex* output_mutex) {
  char buffer[4096];
  for (;;) {
    DWORD count = 0;
    if (!ReadFile(handle, buffer, sizeof(buffer), &count, nullptr) || count == 0) break;
    std::lock_guard<std::mutex> lock(*output_mutex);
    output->append(buffer, count);
  }
  CloseHandle(handle);
}

bool Process::Wait(int32_t* exit_code, std::string* error) {
  if (!impl_ || !impl_->process) {
    SetError(error, "process handle is empty");
    return false;
  }
  std::lock_guard<std::mutex> wait_lock(impl_->wait_mutex);
  bool needs_wait = false;
  {
    std::lock_guard<std::mutex> lock(impl_->state_mutex);
    needs_wait = !impl_->exited;
  }
  if (needs_wait) {
    if (WaitForSingleObject(impl_->process, INFINITE) != WAIT_OBJECT_0) {
      SetError(error, WindowsError("process wait failed"));
      return false;
    }
    DWORD code = 0;
    if (!GetExitCodeProcess(impl_->process, &code)) {
      SetError(error, WindowsError("process exit-code query failed"));
      return false;
    }
    std::lock_guard<std::mutex> lock(impl_->state_mutex);
    impl_->exit_code = static_cast<int32_t>(code);
    impl_->exited = true;
  }
  {
    std::lock_guard<std::mutex> lock(impl_->state_mutex);
    if (exit_code) *exit_code = impl_->exit_code;
  }
  (void)CloseStdin(nullptr);
  if (impl_->stdout_reader.joinable()) impl_->stdout_reader.join();
  if (impl_->stderr_reader.joinable()) impl_->stderr_reader.join();
  if (error) error->clear();
  return true;
}

bool Process::Poll(int32_t* exit_code, bool* exited, std::string* error) {
  if (!impl_ || !impl_->process || !exited) {
    SetError(error, "process handle is empty");
    return false;
  }
  std::lock_guard<std::mutex> wait_lock(impl_->wait_mutex);
  {
    std::lock_guard<std::mutex> lock(impl_->state_mutex);
    *exited = impl_->exited;
  }
  if (!*exited) {
    const DWORD wait = WaitForSingleObject(impl_->process, 0);
    if (wait == WAIT_FAILED) {
      SetError(error, WindowsError("process poll failed"));
      return false;
    }
    if (wait == WAIT_OBJECT_0) {
      DWORD code = 0;
      if (!GetExitCodeProcess(impl_->process, &code)) {
        SetError(error, WindowsError("process exit-code query failed"));
        return false;
      }
      std::lock_guard<std::mutex> lock(impl_->state_mutex);
      impl_->exit_code = static_cast<int32_t>(code);
      impl_->exited = true;
      *exited = true;
    }
  }
  {
    std::lock_guard<std::mutex> lock(impl_->state_mutex);
    if (exit_code) *exit_code = impl_->exited ? impl_->exit_code : -1;
  }
  if (*exited) {
    (void)CloseStdin(nullptr);
    if (impl_->stdout_reader.joinable()) impl_->stdout_reader.join();
    if (impl_->stderr_reader.joinable()) impl_->stderr_reader.join();
  }
  if (error) error->clear();
  return true;
}

bool Process::Kill(std::string* error) {
  if (!impl_ || !impl_->process) {
    SetError(error, "process handle is empty");
    return false;
  }
  std::lock_guard<std::mutex> lock(impl_->state_mutex);
  if (impl_->exited) return true;
  if (!TerminateProcess(impl_->process, 1)) {
    SetError(error, WindowsError("process termination failed"));
    return false;
  }
  if (error) error->clear();
  return true;
}

bool Process::WriteStdin(const std::string& text, std::string* error) {
  if (!impl_) {
    SetError(error, "process stdin is closed");
    return false;
  }
  std::lock_guard<std::mutex> lock(impl_->stdin_mutex);
  if (!impl_->stdin_write) {
    SetError(error, "process stdin is closed");
    return false;
  }
  size_t offset = 0;
  while (offset < text.size()) {
    DWORD wrote = 0;
    const DWORD request = static_cast<DWORD>((std::min)(text.size() - offset,
                                                        static_cast<size_t>(0xffffffffu)));
    if (!WriteFile(impl_->stdin_write, text.data() + offset, request, &wrote, nullptr) ||
        wrote == 0) {
      SetError(error, WindowsError("process stdin write failed"));
      return false;
    }
    offset += wrote;
  }
  if (error) error->clear();
  return true;
}

bool Process::CloseStdin(std::string* error) {
  if (!impl_) return false;
  std::lock_guard<std::mutex> lock(impl_->stdin_mutex);
  CloseNativeHandle(&impl_->stdin_write);
  if (error) error->clear();
  return true;
}

std::string Process::Stdout() const {
  if (!impl_) return {};
  std::lock_guard<std::mutex> lock(impl_->output_mutex);
  return impl_->stdout_text;
}

std::string Process::Stderr() const {
  if (!impl_) return {};
  std::lock_guard<std::mutex> lock(impl_->output_mutex);
  return impl_->stderr_text;
}

bool Process::Close(std::string* error) {
  if (!impl_) return true;
  {
    std::lock_guard<std::mutex> lock(impl_->state_mutex);
    if (impl_->closed) return true;
  }
  int32_t ignored_exit = -1;
  bool exited = false;
  {
    std::lock_guard<std::mutex> lock(impl_->state_mutex);
    exited = impl_->exited;
  }
  if (!exited) (void)Kill(nullptr);
  if (!Wait(&ignored_exit, error)) return false;
  CloseNativeHandle(&impl_->stdout_read);
  CloseNativeHandle(&impl_->stderr_read);
  CloseNativeHandle(&impl_->process);
  {
    std::lock_guard<std::mutex> lock(impl_->state_mutex);
    impl_->closed = true;
  }
  return true;
}

std::shared_ptr<Process> SpawnProcess(const ProcessStartRequest& request,
                                      std::string* error) {
  if (request.program.empty()) {
    SetError(error, "process program is empty");
    return {};
  }
  SECURITY_ATTRIBUTES security{};
  security.nLength = sizeof(security);
  security.bInheritHandle = TRUE;
  HANDLE stdin_read = nullptr;
  HANDLE stdin_write = nullptr;
  HANDLE stdout_read = nullptr;
  HANDLE stdout_write = nullptr;
  HANDLE stderr_read = nullptr;
  HANDLE stderr_write = nullptr;
  auto close_all = [&] {
    CloseNativeHandle(&stdin_read);
    CloseNativeHandle(&stdin_write);
    CloseNativeHandle(&stdout_read);
    CloseNativeHandle(&stdout_write);
    CloseNativeHandle(&stderr_read);
    CloseNativeHandle(&stderr_write);
  };
  if (!CreatePipe(&stdin_read, &stdin_write, &security, 0) ||
      !CreatePipe(&stdout_read, &stdout_write, &security, 0) ||
      !CreatePipe(&stderr_read, &stderr_write, &security, 0)) {
    SetError(error, WindowsError("process pipe creation failed"));
    close_all();
    return {};
  }
  if ((stdin_write && !SetHandleInformation(stdin_write, HANDLE_FLAG_INHERIT, 0)) ||
      (stdout_read && !SetHandleInformation(stdout_read, HANDLE_FLAG_INHERIT, 0)) ||
      (stderr_read && !SetHandleInformation(stderr_read, HANDLE_FLAG_INHERIT, 0))) {
    SetError(error, WindowsError("process pipe inheritance setup failed"));
    close_all();
    return {};
  }

  std::wstring wide_program;
  if (!Utf8ToWide(request.program, &wide_program)) {
    SetError(error, WindowsError("process program is not valid UTF-8"));
    close_all();
    return {};
  }
  std::wstring command_line = QuoteWindowsArgument(wide_program);
  for (const auto& argument : request.arguments) {
    std::wstring wide_argument;
    if (!Utf8ToWide(argument, &wide_argument)) {
      SetError(error, WindowsError("process argument is not valid UTF-8"));
      close_all();
      return {};
    }
    command_line.push_back(L' ');
    command_line += QuoteWindowsArgument(wide_argument);
  }
  std::vector<wchar_t> mutable_command(command_line.begin(), command_line.end());
  mutable_command.push_back(L'\0');
  STARTUPINFOEXW startup{};
  startup.StartupInfo.cb = sizeof(startup);
  startup.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
  startup.StartupInfo.hStdInput = stdin_read;
  startup.StartupInfo.hStdOutput = stdout_write;
  startup.StartupInfo.hStdError = stderr_write;

  SIZE_T attribute_size = 0;
  InitializeProcThreadAttributeList(nullptr, 1, 0, &attribute_size);
  if (attribute_size == 0) {
    SetError(error, WindowsError("process handle-list sizing failed"));
    close_all();
    return {};
  }
  std::vector<uint8_t> attribute_storage(attribute_size);
  startup.lpAttributeList = reinterpret_cast<PPROC_THREAD_ATTRIBUTE_LIST>(attribute_storage.data());
  HANDLE inherited_handles[] = {stdin_read, stdout_write, stderr_write};
  if (!InitializeProcThreadAttributeList(startup.lpAttributeList, 1, 0, &attribute_size)) {
    SetError(error, WindowsError("process handle-list initialization failed"));
    close_all();
    return {};
  }
  if (!UpdateProcThreadAttribute(startup.lpAttributeList, 0,
                                 PROC_THREAD_ATTRIBUTE_HANDLE_LIST, inherited_handles,
                                 sizeof(inherited_handles), nullptr, nullptr)) {
    SetError(error, WindowsError("process handle inheritance setup failed"));
    DeleteProcThreadAttributeList(startup.lpAttributeList);
    close_all();
    return {};
  }
  PROCESS_INFORMATION created{};
  const BOOL process_created = CreateProcessW(
      nullptr, mutable_command.data(), nullptr, nullptr, TRUE, EXTENDED_STARTUPINFO_PRESENT,
      nullptr, nullptr, &startup.StartupInfo, &created);
  DeleteProcThreadAttributeList(startup.lpAttributeList);
  if (!process_created) {
    SetError(error, WindowsError("process spawn failed"));
    close_all();
    return {};
  }
  CloseHandle(created.hThread);
  CloseNativeHandle(&stdin_read);
  CloseNativeHandle(&stdout_write);
  CloseNativeHandle(&stderr_write);

  auto impl = std::make_unique<Process::Impl>();
  impl->process = created.hProcess;
  impl->stdin_write = stdin_write;
  stdin_write = nullptr;
  impl->stdout_read = stdout_read;
  stdout_read = nullptr;
  impl->stderr_read = stderr_read;
  stderr_read = nullptr;
  close_all();
  auto process = std::make_shared<Process>(std::move(impl));
  try {
    if (process->impl_->stdout_read) {
      HANDLE handle = process->impl_->stdout_read;
      process->impl_->stdout_reader =
          std::thread(ReadProcessPipe, handle, &process->impl_->stdout_text,
                      &process->impl_->output_mutex);
      process->impl_->stdout_read = nullptr;
    }
    if (process->impl_->stderr_read) {
      HANDLE handle = process->impl_->stderr_read;
      process->impl_->stderr_reader =
          std::thread(ReadProcessPipe, handle, &process->impl_->stderr_text,
                      &process->impl_->output_mutex);
      process->impl_->stderr_read = nullptr;
    }
  } catch (const std::exception& exception) {
    (void)process->Close(nullptr);
    SetError(error, std::string("process output reader creation failed: ") + exception.what());
    return {};
  }
  if (error) error->clear();
  return process;
}

int64_t OpenDynamicLibrary(const std::string& path, std::string* error) {
  HMODULE handle = LoadLibraryW(std::filesystem::path(path).wstring().c_str());
  if (!handle) { SetError(error, WindowsError("System.FFI.open failed")); return 0; }
  if (error) error->clear();
  return reinterpret_cast<int64_t>(handle);
}
int64_t FindDynamicSymbol(int64_t handle, const std::string& name, std::string* error) {
  if (!handle) { SetError(error, "System.FFI.sym null handle"); return 0; }
  FARPROC symbol = GetProcAddress(reinterpret_cast<HMODULE>(handle), name.c_str());
  if (!symbol) { SetError(error, WindowsError("System.FFI.sym failed")); return 0; }
  if (error) error->clear();
  return reinterpret_cast<int64_t>(symbol);
}
bool CloseDynamicLibrary(int64_t handle, std::string* error) {
  if (!handle) { SetError(error, "System.FFI.close null handle"); return false; }
  if (!FreeLibrary(reinterpret_cast<HMODULE>(handle))) { SetError(error, WindowsError("System.FFI.close failed")); return false; }
  if (error) error->clear();
  return true;
}

bool BuildNativeExecutable(const NativeBuildRequest& request, std::string* error) {
  std::string configured;
  GetEnvironment("CXX", &configured);
  const std::string compiler = configured.empty() ? "cl.exe" : configured;
  std::vector<std::string> arguments = {
      compiler, "/nologo", "/std:c++17", "/O2", "/EHsc", "/MT"};
  for (const auto& include : request.include_dirs) {
    arguments.push_back("/I" + include.string());
  }
  arguments.push_back(request.source.string());
  for (const auto& library : request.libraries) arguments.push_back(library.string());
  arguments.push_back((request.runtime_library_dir / "ffi.lib").string());
  auto extra = SplitArguments(request.extra_link_flags);
  for (auto& argument : extra) {
    if (argument.rfind("-L", 0) == 0) {
      argument = "/LIBPATH:" + argument.substr(2);
    } else if (argument.rfind("-l", 0) == 0) {
      argument = argument.substr(2) + ".lib";
    }
  }
  arguments.insert(arguments.end(), extra.begin(), extra.end());
  if (!extra.empty()) arguments.push_back("ws2_32.lib");
  arguments.push_back("/Fe:" + request.output.string());

  std::vector<const char*> argv;
  argv.reserve(arguments.size() + 1);
  for (const auto& argument : arguments) argv.push_back(argument.c_str());
  argv.push_back(nullptr);
  if (_spawnvp(_P_WAIT, compiler.c_str(), argv.data()) == 0) {
    if (request.dynamic_runtime) {
      std::error_code copy_error;
      const auto runtime_dll = request.runtime_library_dir / "simplevm_runtime.dll";
      std::filesystem::copy_file(
          runtime_dll, request.output.parent_path() / runtime_dll.filename(),
          std::filesystem::copy_options::overwrite_existing, copy_error);
      if (copy_error) {
        SetError(error, "failed to copy the Simple runtime DLL beside the executable");
        return false;
      }
    }
    return true;
  }
  SetError(error, "failed to compile embedded executable with the MSVC C++ toolchain");
  return false;
}

} // namespace Simple::Platform
