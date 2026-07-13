#include "native/fs.h"

#include <filesystem>
#include <fstream>
#include <iterator>

#include "platform/platform.h"

namespace Simple::VM::Native::Fs {

bool ReadText(const std::string& path, std::string* out) {
  if (!out) return false;
  std::ifstream in(path, std::ios::binary);
  if (!in) return false;
  *out = std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  return true;
}

bool WriteText(const std::string& path, const std::string& text) {
  std::ofstream out(path, std::ios::binary);
  if (!out) return false;
  out.write(text.data(), static_cast<std::streamsize>(text.size()));
  return out.good();
}

bool ReadBytes(const std::string& path, std::vector<int32_t>* out) {
  if (!out) return false;
  std::ifstream in(path, std::ios::binary);
  if (!in) return false;
  out->clear();
  char c = 0;
  while (in.get(c)) out->push_back(static_cast<unsigned char>(c));
  return true;
}

bool WriteBytes(const std::string& path, const std::vector<int32_t>& bytes) {
  std::ofstream out(path, std::ios::binary);
  if (!out) return false;
  for (int32_t value : bytes) {
    const char byte = static_cast<char>(static_cast<uint32_t>(value) & 0xffu);
    out.write(&byte, 1);
  }
  return out.good();
}

bool ListDir(const std::string& path, std::vector<std::string>* out) {
  if (!out) return false;
  std::error_code ec;
  std::vector<std::string> values;
  for (const auto& entry : std::filesystem::directory_iterator(path, ec)) {
    if (ec) return false;
    values.push_back(entry.path().filename().generic_string());
  }
  if (ec) return false;
  *out = std::move(values);
  return true;
}

bool CopyPath(const std::string& from, const std::string& to) {
  std::error_code ec;
  const bool ok = std::filesystem::copy_file(from, to, std::filesystem::copy_options::overwrite_existing, ec);
  return ok && !ec;
}

bool Remove(const std::string& path) {
  std::error_code ec;
  const bool ok = std::filesystem::remove(path, ec);
  return ok && !ec;
}

bool Mkdir(const std::string& path) {
  std::error_code ec;
  const bool ok = std::filesystem::create_directory(path, ec) || std::filesystem::is_directory(path, ec);
  return ok && !ec;
}

bool MkdirAll(const std::string& path) {
  std::error_code ec;
  const bool ok = std::filesystem::create_directories(path, ec) || std::filesystem::is_directory(path, ec);
  return ok && !ec;
}

bool SetCwd(const std::string& path) {
  std::error_code ec;
  std::filesystem::current_path(path, ec);
  return !ec;
}

bool Cwd(std::string* out) {
  if (!out) return false;
  std::error_code ec;
  const auto cwd = std::filesystem::current_path(ec);
  if (ec) return false;
  *out = cwd.generic_string();
  return true;
}

std::FILE* OpenFile(const std::string& path, const char* mode) {
  return Simple::Platform::OpenFile(path, mode);
}

} // namespace Simple::VM::Native::Fs
