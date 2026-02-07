#pragma once

#include <cstddef>
#include <string>

namespace Simple::VM::Tests {

int RunSirFile(const std::string& path, bool verify);
int RunSirPerfDir(const std::string& dir, size_t iterations, bool verify);

} // namespace Simple::VM::Tests
