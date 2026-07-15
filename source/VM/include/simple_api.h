#ifndef SIMPLE_VM_API_H
#define SIMPLE_VM_API_H

#include <cstdint>

namespace Simple::VM {
constexpr uint16_t kRuntimeAbiVersionMajor = 1;
constexpr uint16_t kRuntimeAbiVersionMinor = 8;
} // namespace Simple::VM

#if defined(_WIN32) || defined(__CYGWIN__)
#  if defined(SIMPLEVM_SHARED)
#    if defined(SIMPLEVM_BUILDING_DLL)
#      define SIMPLEVM_API __declspec(dllexport)
#    else
#      define SIMPLEVM_API __declspec(dllimport)
#    endif
#  else
#    define SIMPLEVM_API
#  endif
#else
#  if __GNUC__ >= 4
#    define SIMPLEVM_API __attribute__((visibility("default")))
#  else
#    define SIMPLEVM_API
#  endif
#endif

#endif // SIMPLE_VM_API_H
