#ifndef SIMPLE_VM_NATIVE_RANDOM_H
#define SIMPLE_VM_NATIVE_RANDOM_H

#include <cstdint>

namespace Simple::VM::Native::Random {

void Seed(uint64_t value);
int32_t I32();
int64_t I64();
int32_t Range(int32_t lo, int32_t hi);
double F64();

} // namespace Simple::VM::Native::Random

#endif // SIMPLE_VM_NATIVE_RANDOM_H
