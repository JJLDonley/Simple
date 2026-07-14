#pragma once

#include <cerrno>
#include <cctype>
#include <csignal>
#include <cstring>
#include <exception>
#include <fcntl.h>
#include <mutex>
#include <pthread.h>
#include <spawn.h>
#include <string>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>
#include <utility>
#include <vector>

#include "platform/platform.h"

extern char** environ;

namespace Simple::Platform::Posix {

inline std::vector<std::string> SplitArguments(const std::string& text) {
  std::vector<std::string> arguments;
  std::string current;
  char quote = '\0';
  for (char c : text) {
    if ((c == '"' || c == '\'') && (quote == '\0' || quote == c)) {
      quote = quote == '\0' ? c : '\0';
    } else if (std::isspace(static_cast<unsigned char>(c)) && quote == '\0') {
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

inline int RunProcess(const std::vector<std::string>& arguments) {
  if (arguments.empty()) return -1;
  std::vector<char*> argv;
  argv.reserve(arguments.size() + 1);
  for (const auto& argument : arguments) argv.push_back(const_cast<char*>(argument.c_str()));
  argv.push_back(nullptr);
  pid_t child = -1;
  if (posix_spawnp(&child, argv[0], nullptr, nullptr, argv.data(), environ) != 0) return -1;
  int status = 0;
  if (waitpid(child, &status, 0) < 0) return -1;
  return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}

inline void CloseFd(int* fd) {
  if (!fd || *fd < 0) return;
  close(*fd);
  *fd = -1;
}

inline std::string ErrnoText(const char* operation, int code = errno) {
  return std::string(operation) + ": " + std::strerror(code);
}

inline bool SetCloseOnExec(int fd) {
  const int flags = fcntl(fd, F_GETFD);
  return flags >= 0 && fcntl(fd, F_SETFD, flags | FD_CLOEXEC) == 0;
}

inline bool WritePipe(int fd, const std::string& text, std::string* error) {
  sigset_t blocked;
  sigset_t previous;
  sigemptyset(&blocked);
  sigaddset(&blocked, SIGPIPE);
  if (pthread_sigmask(SIG_BLOCK, &blocked, &previous) != 0) {
    if (error) *error = "process stdin signal-mask setup failed";
    return false;
  }
  sigset_t pending;
  sigemptyset(&pending);
  sigpending(&pending);
  const bool was_pending = sigismember(&pending, SIGPIPE) == 1;

  size_t offset = 0;
  int write_error = 0;
  while (offset < text.size()) {
    const ssize_t wrote = write(fd, text.data() + offset, text.size() - offset);
    if (wrote > 0) {
      offset += static_cast<size_t>(wrote);
      continue;
    }
    if (wrote < 0 && errno == EINTR) continue;
    write_error = errno;
    break;
  }
  if (write_error == EPIPE && !was_pending) {
    struct timespec no_wait {};
    (void)sigtimedwait(&blocked, nullptr, &no_wait);
  }
  (void)pthread_sigmask(SIG_SETMASK, &previous, nullptr);
  if (write_error == 0) {
    if (error) error->clear();
    return true;
  }
  if (error) *error = ErrnoText("process stdin write failed", write_error);
  return false;
}

inline void ReadPipe(int fd, std::string* output, std::mutex* output_mutex) {
  char buffer[4096];
  for (;;) {
    const ssize_t count = read(fd, buffer, sizeof(buffer));
    if (count > 0) {
      std::lock_guard<std::mutex> lock(*output_mutex);
      output->append(buffer, static_cast<size_t>(count));
      continue;
    }
    if (count < 0 && errno == EINTR) continue;
    break;
  }
  close(fd);
}

} // namespace Simple::Platform::Posix

namespace Simple::Platform {

struct Process::Impl {
  pid_t pid = -1;
  int stdin_fd = -1;
  int stdout_fd = -1;
  int stderr_fd = -1;
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
bool Process::Wait(int32_t* exit_code, std::string* error) {
  if (!impl_) {
    if (error) *error = "process handle is empty";
    return false;
  }
  std::lock_guard<std::mutex> wait_lock(impl_->wait_mutex);
  bool needs_wait = false;
  {
    std::lock_guard<std::mutex> lock(impl_->state_mutex);
    needs_wait = !impl_->exited;
  }
  if (needs_wait) {
    int status = 0;
    pid_t waited = -1;
    do {
      waited = waitpid(impl_->pid, &status, 0);
    } while (waited < 0 && errno == EINTR);
    if (waited < 0) {
      if (error) *error = Posix::ErrnoText("waitpid failed");
      return false;
    }
    std::lock_guard<std::mutex> lock(impl_->state_mutex);
    impl_->exited = true;
    impl_->exit_code = WIFEXITED(status)
                           ? WEXITSTATUS(status)
                           : (WIFSIGNALED(status) ? 128 + WTERMSIG(status) : -1);
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
  if (!impl_ || !exited) {
    if (error) *error = "process handle is empty";
    return false;
  }
  std::lock_guard<std::mutex> wait_lock(impl_->wait_mutex);
  {
    std::lock_guard<std::mutex> lock(impl_->state_mutex);
    *exited = impl_->exited;
  }
  if (!*exited) {
    int status = 0;
    const pid_t waited = waitpid(impl_->pid, &status, WNOHANG);
    if (waited < 0) {
      if (error) *error = Posix::ErrnoText("process poll failed");
      return false;
    }
    if (waited > 0) {
      std::lock_guard<std::mutex> lock(impl_->state_mutex);
      impl_->exited = true;
      impl_->exit_code = WIFEXITED(status)
                             ? WEXITSTATUS(status)
                             : (WIFSIGNALED(status) ? 128 + WTERMSIG(status) : -1);
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
  if (!impl_) {
    if (error) *error = "process handle is empty";
    return false;
  }
  std::lock_guard<std::mutex> lock(impl_->state_mutex);
  if (impl_->exited) return true;
  if (kill(impl_->pid, SIGKILL) != 0 && errno != ESRCH) {
    if (error) *error = Posix::ErrnoText("kill failed");
    return false;
  }
  if (error) error->clear();
  return true;
}

bool Process::WriteStdin(const std::string& text, std::string* error) {
  if (!impl_) {
    if (error) *error = "process stdin is closed";
    return false;
  }
  std::lock_guard<std::mutex> lock(impl_->stdin_mutex);
  if (impl_->stdin_fd < 0) {
    if (error) *error = "process stdin is closed";
    return false;
  }
  return Posix::WritePipe(impl_->stdin_fd, text, error);
}

bool Process::CloseStdin(std::string* error) {
  if (!impl_) return false;
  std::lock_guard<std::mutex> lock(impl_->stdin_mutex);
  Posix::CloseFd(&impl_->stdin_fd);
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
  Posix::CloseFd(&impl_->stdout_fd);
  Posix::CloseFd(&impl_->stderr_fd);
  {
    std::lock_guard<std::mutex> lock(impl_->state_mutex);
    impl_->closed = true;
  }
  return true;
}

std::shared_ptr<Process> SpawnProcess(const ProcessStartRequest& request,
                                      std::string* error) {
  if (request.program.empty()) {
    if (error) *error = "process program is empty";
    return {};
  }
  int stdin_pipe[2] = {-1, -1};
  int stdout_pipe[2] = {-1, -1};
  int stderr_pipe[2] = {-1, -1};
  auto close_all = [&] {
    Posix::CloseFd(&stdin_pipe[0]);
    Posix::CloseFd(&stdin_pipe[1]);
    Posix::CloseFd(&stdout_pipe[0]);
    Posix::CloseFd(&stdout_pipe[1]);
    Posix::CloseFd(&stderr_pipe[0]);
    Posix::CloseFd(&stderr_pipe[1]);
  };
  if (pipe(stdin_pipe) != 0 || pipe(stdout_pipe) != 0 || pipe(stderr_pipe) != 0) {
    if (error) *error = Posix::ErrnoText("process pipe creation failed");
    close_all();
    return {};
  }
  for (int fd : {stdin_pipe[0], stdin_pipe[1], stdout_pipe[0], stdout_pipe[1],
                 stderr_pipe[0], stderr_pipe[1]}) {
    if (!Posix::SetCloseOnExec(fd)) {
      if (error) *error = Posix::ErrnoText("process pipe close-on-exec setup failed");
      close_all();
      return {};
    }
  }

  posix_spawn_file_actions_t actions;
  if (posix_spawn_file_actions_init(&actions) != 0) {
    if (error) *error = "process spawn actions initialization failed";
    close_all();
    return {};
  }
  auto add_pipe_actions = [&](int child_fd, int parent_end, int child_end) {
    return posix_spawn_file_actions_adddup2(&actions, child_end, child_fd) == 0 &&
           posix_spawn_file_actions_addclose(&actions, parent_end) == 0 &&
           posix_spawn_file_actions_addclose(&actions, child_end) == 0;
  };
  bool actions_ready = add_pipe_actions(STDIN_FILENO, stdin_pipe[1], stdin_pipe[0]) &&
                       add_pipe_actions(STDOUT_FILENO, stdout_pipe[0], stdout_pipe[1]) &&
                       add_pipe_actions(STDERR_FILENO, stderr_pipe[0], stderr_pipe[1]);
#if defined(__linux__) || defined(__APPLE__)
  actions_ready = actions_ready && posix_spawn_file_actions_addclosefrom_np(&actions, 3) == 0;
#endif
  if (!actions_ready) {
    if (error) *error = "process spawn actions setup failed";
    posix_spawn_file_actions_destroy(&actions);
    close_all();
    return {};
  }

  std::vector<std::string> storage;
  storage.reserve(request.arguments.size() + 1);
  storage.push_back(request.program);
  storage.insert(storage.end(), request.arguments.begin(), request.arguments.end());
  std::vector<char*> argv;
  argv.reserve(storage.size() + 1);
  for (auto& argument : storage) argv.push_back(argument.data());
  argv.push_back(nullptr);

  pid_t pid = -1;
  const int spawn_error =
      posix_spawnp(&pid, request.program.c_str(), &actions, nullptr, argv.data(), environ);
  posix_spawn_file_actions_destroy(&actions);
  if (spawn_error != 0) {
    if (error) *error = Posix::ErrnoText("process spawn failed", spawn_error);
    close_all();
    return {};
  }

  auto impl = std::make_unique<Process::Impl>();
  impl->pid = pid;
  Posix::CloseFd(&stdin_pipe[0]);
  impl->stdin_fd = stdin_pipe[1];
  stdin_pipe[1] = -1;
  Posix::CloseFd(&stdout_pipe[1]);
  impl->stdout_fd = stdout_pipe[0];
  stdout_pipe[0] = -1;
  Posix::CloseFd(&stderr_pipe[1]);
  impl->stderr_fd = stderr_pipe[0];
  stderr_pipe[0] = -1;
  close_all();

  auto process = std::make_shared<Process>(std::move(impl));
  try {
    if (process->impl_->stdout_fd >= 0) {
      const int fd = process->impl_->stdout_fd;
      process->impl_->stdout_reader =
          std::thread(Posix::ReadPipe, fd, &process->impl_->stdout_text,
                      &process->impl_->output_mutex);
      process->impl_->stdout_fd = -1;
    }
    if (process->impl_->stderr_fd >= 0) {
      const int fd = process->impl_->stderr_fd;
      process->impl_->stderr_reader =
          std::thread(Posix::ReadPipe, fd, &process->impl_->stderr_text,
                      &process->impl_->output_mutex);
      process->impl_->stderr_fd = -1;
    }
  } catch (const std::exception& exception) {
    (void)process->Close(nullptr);
    if (error) *error = std::string("process output reader creation failed: ") + exception.what();
    return {};
  }
  if (error) error->clear();
  return process;
}

} // namespace Simple::Platform
