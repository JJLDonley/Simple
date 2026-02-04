#pragma once

#include <string>

namespace Simple::VM::Tests {

int RunSimpleFile(const std::string& path, bool verify);
int RunSimplePerfDir(const std::string& dir, size_t iterations, bool verify);

} // namespace Simple::VM::Tests
