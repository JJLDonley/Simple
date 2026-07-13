#pragma once

#include <cctype>
#include <string>
#include <sys/wait.h>
#include <unistd.h>
#include <utility>
#include <vector>

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
  const pid_t child = fork();
  if (child < 0) return -1;
  if (child == 0) {
    std::vector<char*> argv;
    argv.reserve(arguments.size() + 1);
    for (const auto& argument : arguments) {
      argv.push_back(const_cast<char*>(argument.c_str()));
    }
    argv.push_back(nullptr);
    execvp(argv[0], argv.data());
    _exit(127);
  }
  int status = 0;
  if (waitpid(child, &status, 0) < 0) return -1;
  return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}

} // namespace Simple::Platform::Posix
