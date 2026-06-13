#include "native/os.h"

#include <filesystem>

namespace Simple::VM::Native::Os {

bool CurrentWorkingDirectory(std::string* out) {
  if (!out) return false;
  try {
    *out = std::filesystem::current_path().u8string();
    return true;
  } catch (...) {
    out->clear();
    return false;
  }
}

} // namespace Simple::VM::Native::Os
