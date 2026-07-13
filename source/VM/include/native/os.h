#ifndef SIMPLE_VM_NATIVE_OS_H
#define SIMPLE_VM_NATIVE_OS_H

#include <string>

namespace Simple::VM::Native::Os {

bool CurrentWorkingDirectory(std::string* out);

} // namespace Simple::VM::Native::Os

#endif // SIMPLE_VM_NATIVE_OS_H
