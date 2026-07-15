#ifndef SIMPLE_VM_RUNTIME_EXTERNAL_U8_H
#define SIMPLE_VM_RUNTIME_EXTERNAL_U8_H

#include <string>

namespace Simple::VM {
struct HeapObject;
}

namespace Simple::VM::Runtime {

bool BuildExternalU8String(const HeapObject* object,
                           std::string* out,
                           std::string* error);

} // namespace Simple::VM::Runtime

#endif // SIMPLE_VM_RUNTIME_EXTERNAL_U8_H
