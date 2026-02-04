#pragma once

#include <string>

namespace Simple::VM::Tests {

int RunSimpleFile(const std::string& path, bool verify);
int RunSimplePerfDir(const std::string& dir, size_t iterations, bool verify);
bool RunSimpleFileExpectError(const std::string& path, const std::string& contains);
bool RunSimpleFileExpectTrap(const std::string& path, const std::string& contains);

} // namespace Simple::VM::Tests
