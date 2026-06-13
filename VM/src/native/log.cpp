#include "native/log.h"

#include <atomic>
#include <fstream>
#include <iostream>
#include <memory>
#include <mutex>

namespace Simple::VM::Native::Log {
namespace {

std::atomic<int32_t> g_level{0};
std::mutex g_file_mutex;
std::unique_ptr<std::ofstream> g_file;

const char* LabelForLevel(int32_t level) {
  return level >= 3 ? "ERROR" : (level == 2 ? "WARN" : "INFO");
}

} // namespace

void SetLevel(int32_t level) {
  g_level.store(level);
}

bool SetFile(const std::string& path) {
  std::lock_guard<std::mutex> lock(g_file_mutex);
  g_file.reset();
  if (path.empty()) return true;
  auto file = std::make_unique<std::ofstream>(path, std::ios::out | std::ios::trunc);
  if (!file->is_open()) return false;
  g_file = std::move(file);
  return true;
}

void Emit(const std::string& message, int32_t level) {
  if (level < g_level.load()) return;
  const char* label = LabelForLevel(level);
  std::lock_guard<std::mutex> lock(g_file_mutex);
  if (g_file && g_file->is_open()) {
    (*g_file) << "[" << label << "] " << message << "\n";
    g_file->flush();
    return;
  }
  std::ostream& stream = level >= 2 ? std::cerr : std::cout;
  stream << "[" << label << "] " << message << "\n";
}

} // namespace Simple::VM::Native::Log
